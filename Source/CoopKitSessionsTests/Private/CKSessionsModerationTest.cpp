// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsLibrary.h"
#include "CKSessionsNetTestSession.h"
#include "CKSessionsOnlineSettings.h"
#include "CKSessionsSubsystem.h"
#include "CKSessionsTestQuickPlayRecorder.h"
#include "CKSessionsTestRemovalRecorder.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CKSessions::Tests
{
	namespace
	{
		const TCHAR* ModerationMap = TEXT("/CoopKitSessions/Maps/L_SessionsDemo");

		/** One Null search (5 s) plus room for a busy machine, at the 30 fps cap of the session. */
		constexpr int32 ModerationSearchTicks = 300;

		/** Opening the map as a listen server, or resolving, travelling and logging in. */
		constexpr int32 ModerationTravelTicks = 300;

		/** Ticks to watch that nothing happens: longer than the plugin's 2 s pause before a reconnect attempt. */
		constexpr int32 ModerationQuietTicks = 150;

		using FRemovalRecorderRef = TSharedRef<TStrongObjectPtr<UCKSessionsTestRemovalRecorder>>;

		FRemovalRecorderRef MakeRemovalRecorder()
		{
			return MakeShared<TStrongObjectPtr<UCKSessionsTestRemovalRecorder>>(NewObject<UCKSessionsTestRemovalRecorder>(GetTransientPackage()));
		}

		TSoftObjectPtr<UWorld> GetModerationMap()
		{
			return TSoftObjectPtr<UWorld>(FSoftObjectPath(FString(ModerationMap) + TEXT(".L_SessionsDemo")));
		}

		UCKSessionsSubsystem* ModerationSessionsOf(UGameInstance* GameInstance)
		{
			return GameInstance != nullptr ? GameInstance->GetSubsystem<UCKSessionsSubsystem>() : nullptr;
		}

		int32 ModerationCountPlayersIn(const UWorld* World)
		{
			return World != nullptr && World->GetGameState() != nullptr ? World->GetGameState()->PlayerArray.Num() : -1;
		}

		/** Player state of client A on the host, found by the unique id of the player state client A sees. */
		APlayerState* FindServerStateOfClientA(const FNetTestSession& Session)
		{
			const UWorld* ClientWorld = Session.GetClientWorld(0);
			const APlayerController* ClientController = ClientWorld != nullptr ? ClientWorld->GetFirstPlayerController() : nullptr;
			const APlayerState* ClientState = ClientController != nullptr ? ClientController->PlayerState.Get() : nullptr;
			UWorld* ServerWorld = Session.GetServerWorld();
			if (ClientState == nullptr || ServerWorld == nullptr || ServerWorld->GetGameState() == nullptr)
			{
				return nullptr;
			}
			for (APlayerState* State : ServerWorld->GetGameState()->PlayerArray)
			{
				if (State != nullptr && State->GetUniqueId() == ClientState->GetUniqueId())
				{
					return State;
				}
			}
			return nullptr;
		}

		FNetStep MakeHostStep(const TSharedRef<FNetTestSession>& Session)
		{
			return { TEXT("Host opens the map as a listen server and creates the session"),
				[Session]()
				{
					ModerationSessionsOf(Session->GetHostGameInstance())->HostSession(nullptr, GetModerationMap());
				},
				[Session]()
				{
					const UWorld* World = Session->GetServerWorld();
					return ModerationSessionsOf(Session->GetHostGameInstance())->GetState() == ECKSessionsState::InSession
						&& World != nullptr && World->GetNetMode() == NM_ListenServer;
				},
				ModerationTravelTicks };
		}

		/** Client A finds the host's session and joins it; the host then sees two players. */
		void AddFindAndJoinSteps(TArray<FNetStep>& Steps, const TSharedRef<FNetTestSession>& Session, const FString& Label)
		{
			Steps.Add({ Label + TEXT(": client A finds the session"),
				[Session]()
				{
					ModerationSessionsOf(Session->GetClientGameInstance(0))->FindSessions(nullptr);
				},
				[Session]()
				{
					const UCKSessionsSubsystem* Sessions = ModerationSessionsOf(Session->GetClientGameInstance(0));
					return Sessions->GetState() == ECKSessionsState::Idle && Sessions->GetSearchResults().Num() == 1;
				},
				ModerationSearchTicks });
			Steps.Add({ Label + TEXT(": client A joins and the host sees the player"),
				[Session]()
				{
					UCKSessionsSubsystem* Sessions = ModerationSessionsOf(Session->GetClientGameInstance(0));
					Sessions->JoinSession(Sessions->GetSearchResults()[0]);
				},
				[Session]()
				{
					return ModerationSessionsOf(Session->GetClientGameInstance(0))->GetState() == ECKSessionsState::InSession && ModerationCountPlayersIn(Session->GetServerWorld()) == 2;
				},
				ModerationTravelTicks });
		}

		/** Passes after Ticks frames; holds the step so that anything the plugin does late (a reconnect) has time to show. */
		FNetStep MakeQuietStep(const FString& Name, const TFunction<void()>& Check)
		{
			const TSharedRef<int32> Waited = MakeShared<int32>(0);
			return { Name, [Waited]() { *Waited = 0; },
				[Waited, Check]()
				{
					if (++(*Waited) < ModerationQuietTicks)
					{
						return false;
					}
					Check();
					return true;
				},
				ModerationQuietTicks + 30 };
		}

		void ExpectEngineFailures(FAutomationTestBase& Test, const TCHAR* RemovalText)
		{
			// The removal is part of the scenario: the engine reports a refused or closed connection as an error and a failed travel.
			Test.AddExpectedErrorPlain(TEXT("UEngine::BroadcastNetworkFailure: FailureType = "), EAutomationExpectedErrorFlags::Contains, 0);
			Test.AddExpectedErrorPlain(RemovalText, EAutomationExpectedErrorFlags::Contains, 0);
		}

		bool StartScenario(FAutomationTestBase& Test, const TSharedRef<FNetTestSession>& Session, TArray<FNetStep>&& Steps, const TCHAR* What)
		{
			if (!Session->Start(Test, ModerationMap))
			{
				return false;
			}
			Test.AddInfo(FString::Printf(TEXT("Scenario: %s, %s, budgets: search %d, travel %d ticks"), *Session->Describe(), What, ModerationSearchTicks, ModerationTravelTicks));
			Test.AddCommand(new FNetScenarioCommand(Test, Session, MoveTemp(Steps)));
			return true;
		}

		FNetSessionSettings MakeListenSettings(const int32 RemoteClients)
		{
			FNetSessionSettings Settings;
			Settings.Host = ENetSessionHost::ListenServer;
			Settings.ClientConnect = ENetClientConnect::Manual;
			Settings.RemoteClients = RemoteClients;
			return Settings;
		}

		/** Host kicks client A with a reason: A is told, does not reconnect, and may come back by searching (a kick is not a ban). */
		bool RunKickScenario(FAutomationTestBase& Test)
		{
			const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(MakeListenSettings(1));
			const FRemovalRecorderRef Recorder = MakeRemovalRecorder();

			TArray<FNetStep> Steps;
			Steps.Add(MakeHostStep(Session));
			Steps.Add({ TEXT("Client A starts listening to its sessions subsystem"),
				[Session, Recorder]()
				{
					(*Recorder)->Watch(*ModerationSessionsOf(Session->GetClientGameInstance(0)));
				},
				[]() { return true; }, 1 });
			AddFindAndJoinSteps(Steps, Session, TEXT("First join"));

			Steps.Add({ TEXT("Host kicks client A with a reason"),
				[TestPtr = &Test, Session]()
				{
					UCKSessionsSubsystem* HostSessions = ModerationSessionsOf(Session->GetHostGameInstance());
					APlayerState* StateOfA = FindServerStateOfClientA(*Session);
					TestPtr->TestNotNull(TEXT("Host finds the player state of client A"), StateOfA);
					TestPtr->TestTrue(TEXT("Host kicks client A"), StateOfA != nullptr && HostSessions->KickPlayer(StateOfA, TEXT("AFK too long")));
					TestPtr->TestFalse(TEXT("A client cannot kick the host's own player"),
						HostSessions->KickPlayer(Session->GetServerWorld()->GetFirstPlayerController()->PlayerState, TEXT("no")));
				},
				[Recorder]()
				{
					return (*Recorder)->HasSessionFailed();
				},
				ModerationTravelTicks });

			Steps.Add(MakeQuietStep(TEXT("Client A is told it was kicked, with the reason, and does not reconnect"),
				[TestPtr = &Test, Session, Recorder]()
				{
					const UCKSessionsSubsystem* Sessions = ModerationSessionsOf(Session->GetClientGameInstance(0));
					TestPtr->TestEqual(TEXT("Failure"), (*Recorder)->GetSessionFailure(), ECKSessionsFailure::Kicked);
					TestPtr->TestEqual(TEXT("Reason on On Session Failure"), (*Recorder)->GetSessionMessage(), FString(TEXT("AFK too long")));
					TestPtr->TestEqual(TEXT("Reason on the subsystem"), Sessions->GetLastRemovalReason(), FString(TEXT("AFK too long")));
					TestPtr->TestEqual(TEXT("Last failure"), Sessions->GetLastFailure(), ECKSessionsFailure::Kicked);
					TestPtr->TestFalse(TEXT("No reconnect attempt"), (*Recorder)->WasReconnecting());
					TestPtr->TestEqual(TEXT("Client A is idle"), Sessions->GetState(), ECKSessionsState::Idle);
					TestPtr->TestEqual(TEXT("The host sees only itself"), ModerationCountPlayersIn(Session->GetServerWorld()), 1);
					TestPtr->TestEqual(TEXT("The host keeps only its own record, none of the kicked player"), ModerationSessionsOf(Session->GetHostGameInstance())->GetPlayerRecords().Num(), 1);
				}));

			Steps.Add({ TEXT("Client A forgets the kick"),
				[Recorder]() { (*Recorder)->Reset(); },
				[]() { return true; }, 1 });
			AddFindAndJoinSteps(Steps, Session, TEXT("After the kick"));

			// Kicking the host's own player is refused with one warning.
			Test.AddExpectedMessagePlain(TEXT("only the host removes a remote player"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
			ExpectEngineFailures(Test, TEXT("Kicked from the session"));
			return StartScenario(Test, Session, MoveTemp(Steps), TEXT("kick"));
		}

		/**
		 * Host bans client A: A is told, and cannot come back by a search and join, a reconnect or the host's address, until the
		 * host unbans it.
		 */
		bool RunBanScenario(FAutomationTestBase& Test)
		{
			const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(MakeListenSettings(1));
			const FRemovalRecorderRef Recorder = MakeRemovalRecorder();
			const TSharedRef<FString> BannedId = MakeShared<FString>();
			const TSharedRef<FString> HostAddress = MakeShared<FString>();

			TArray<FNetStep> Steps;
			Steps.Add(MakeHostStep(Session));
			Steps.Add({ TEXT("Client A starts listening to its sessions subsystem"),
				[Session, Recorder, HostAddress]()
				{
					(*Recorder)->Watch(*ModerationSessionsOf(Session->GetClientGameInstance(0)));
					const UWorld* HostWorld = Session->GetServerWorld();
					UNetDriver* Driver = HostWorld != nullptr ? HostWorld->GetNetDriver() : nullptr;
					if (Driver != nullptr)
					{
						// "0.0.0.0:7777" or "ip:port": the port is what a direct connection needs.
						FString Address = Driver->LowLevelGetNetworkNumber();
						int32 ColonIndex = INDEX_NONE;
						if (Address.FindLastChar(TEXT(':'), ColonIndex))
						{
							*HostAddress = TEXT("127.0.0.1") + Address.RightChop(ColonIndex);
						}
					}
				},
				[TestPtr = &Test, HostAddress]()
				{
					TestPtr->TestFalse(TEXT("The host's listen address is known"), HostAddress->IsEmpty());
					return true;
				},
				1 });
			AddFindAndJoinSteps(Steps, Session, TEXT("First join"));

			Steps.Add({ TEXT("Host bans client A with a reason"),
				[TestPtr = &Test, Session, BannedId]()
				{
					UCKSessionsSubsystem* HostSessions = ModerationSessionsOf(Session->GetHostGameInstance());
					APlayerState* StateOfA = FindServerStateOfClientA(*Session);
					TestPtr->TestNotNull(TEXT("Host finds the player state of client A"), StateOfA);
					TestPtr->TestTrue(TEXT("Host bans client A"), StateOfA != nullptr && HostSessions->BanPlayer(StateOfA, TEXT("Griefing")));
					const TArray<FCKSessionsBanRecord> Bans = HostSessions->GetBannedPlayers();
					TestPtr->TestEqual(TEXT("One ban on the list"), Bans.Num(), 1);
					if (Bans.Num() == 1)
					{
						*BannedId = Bans[0].PlayerId;
						TestPtr->TestEqual(TEXT("The ban keeps the reason"), Bans[0].Reason, FString(TEXT("Griefing")));
						TestPtr->TestTrue(TEXT("The id is banned"), HostSessions->IsPlayerBanned(Bans[0].PlayerId));
					}
				},
				[Recorder]()
				{
					return (*Recorder)->HasSessionFailed();
				},
				ModerationTravelTicks });

			Steps.Add(MakeQuietStep(TEXT("Client A is told it was banned, with the reason, and does not reconnect"),
				[TestPtr = &Test, Session, Recorder]()
				{
					const UCKSessionsSubsystem* Sessions = ModerationSessionsOf(Session->GetClientGameInstance(0));
					TestPtr->TestEqual(TEXT("Failure"), (*Recorder)->GetSessionFailure(), ECKSessionsFailure::Banned);
					TestPtr->TestEqual(TEXT("Reason on On Session Failure"), (*Recorder)->GetSessionMessage(), FString(TEXT("Griefing")));
					TestPtr->TestFalse(TEXT("No reconnect attempt"), (*Recorder)->WasReconnecting());
					TestPtr->TestEqual(TEXT("Client A is idle"), Sessions->GetState(), ECKSessionsState::Idle);
					TestPtr->TestEqual(TEXT("The host sees only itself"), ModerationCountPlayersIn(Session->GetServerWorld()), 1);
				}));

			Steps.Add({ TEXT("Client A searches again and still finds the session (a ban is checked at the connection)"),
				[Session, Recorder]()
				{
					(*Recorder)->Reset();
					ModerationSessionsOf(Session->GetClientGameInstance(0))->FindSessions(nullptr);
				},
				[Session]()
				{
					const UCKSessionsSubsystem* Sessions = ModerationSessionsOf(Session->GetClientGameInstance(0));
					return Sessions->GetState() == ECKSessionsState::Idle && Sessions->GetSearchResults().Num() == 1;
				},
				ModerationSearchTicks });

			Steps.Add({ TEXT("Client A joins the session it found and is refused with the reason"),
				[Session]()
				{
					UCKSessionsSubsystem* Sessions = ModerationSessionsOf(Session->GetClientGameInstance(0));
					Sessions->JoinSession(Sessions->GetSearchResults()[0]);
				},
				[TestPtr = &Test, Session, Recorder]()
				{
					if (!(*Recorder)->HasJoinCompleted())
					{
						return false;
					}
					const UCKSessionsSubsystem* Sessions = ModerationSessionsOf(Session->GetClientGameInstance(0));
					TestPtr->TestFalse(TEXT("The join failed"), (*Recorder)->DidJoinSucceed());
					TestPtr->TestEqual(TEXT("Join failure"), (*Recorder)->GetJoinFailure(), ECKSessionsFailure::Banned);
					TestPtr->TestEqual(TEXT("Reason of the ban"), Sessions->GetLastRemovalReason(), FString(TEXT("Griefing")));
					TestPtr->TestEqual(TEXT("The host still sees only itself"), ModerationCountPlayersIn(Session->GetServerWorld()), 1);
					return true;
				},
				ModerationTravelTicks });

			Steps.Add({ TEXT("Client A opens the host's address directly"),
				[Session, HostAddress]()
				{
					const UWorld* WorldOfA = Session->GetClientWorld(0);
					if (APlayerController* Controller = WorldOfA != nullptr ? WorldOfA->GetFirstPlayerController() : nullptr)
					{
						Controller->ClientTravel(*HostAddress, TRAVEL_Absolute);
					}
				},
				[]() { return true; }, 1 });
			Steps.Add(MakeQuietStep(TEXT("The host refuses the direct connection too"),
				[TestPtr = &Test, Session]()
				{
					TestPtr->TestEqual(TEXT("The host still sees only itself"), ModerationCountPlayersIn(Session->GetServerWorld()), 1);
				}));

			Steps.Add({ TEXT("Host unbans client A"),
				[TestPtr = &Test, Session, BannedId]()
				{
					UCKSessionsSubsystem* HostSessions = ModerationSessionsOf(Session->GetHostGameInstance());
					TestPtr->TestFalse(TEXT("Unbanning an unknown id does nothing"), HostSessions->UnbanPlayer(TEXT("nobody")));
					TestPtr->TestTrue(TEXT("Host unbans client A"), HostSessions->UnbanPlayer(*BannedId));
					TestPtr->TestEqual(TEXT("The ban list is empty"), HostSessions->GetBannedPlayers().Num(), 0);
					TestPtr->TestFalse(TEXT("The id is no longer banned"), HostSessions->IsPlayerBanned(*BannedId));
				},
				[]() { return true; }, 1 });
			Steps.Add({ TEXT("Client A forgets the ban"),
				[Session, Recorder]()
				{
					(*Recorder)->Reset();
				},
				[]() { return true; }, 1 });
			AddFindAndJoinSteps(Steps, Session, TEXT("After the unban"));

			ExpectEngineFailures(Test, TEXT("Banned from the session"));
			return StartScenario(Test, Session, MoveTemp(Steps), TEXT("ban"));
		}

		/**
		 * Quick play for a group on two listen servers in one PIE: the group joins a session only when it has room for all, otherwise
		 * the group's player hosts. Client B hosts a session first; its open slots set the size of the groups that fit.
		 */
		bool RunGroupScenario(FAutomationTestBase& Test)
		{
			const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(MakeListenSettings(3));
			const TSharedRef<int32> OpenSlots = MakeShared<int32>(0);
			using FQuickPlayRecorderRef = TSharedRef<TStrongObjectPtr<UCKSessionsTestQuickPlayRecorder>>;
			const FQuickPlayRecorderRef TooBig = MakeShared<TStrongObjectPtr<UCKSessionsTestQuickPlayRecorder>>(NewObject<UCKSessionsTestQuickPlayRecorder>(GetTransientPackage()));
			const FQuickPlayRecorderRef Fits = MakeShared<TStrongObjectPtr<UCKSessionsTestQuickPlayRecorder>>(NewObject<UCKSessionsTestQuickPlayRecorder>(GetTransientPackage()));

			TArray<FNetStep> Steps;
			Steps.Add({ TEXT("Host hosts a session and client B hosts another, both listen servers"),
				[Session]()
				{
					ModerationSessionsOf(Session->GetHostGameInstance())->HostSession(nullptr, GetModerationMap());
					ModerationSessionsOf(Session->GetClientGameInstance(1))->HostSession(nullptr, GetModerationMap());
				},
				[Session]()
				{
					const UWorld* HostWorld = Session->GetServerWorld();
					const UWorld* WorldOfB = Session->GetClientWorld(1);
					return ModerationSessionsOf(Session->GetHostGameInstance())->GetState() == ECKSessionsState::InSession
						&& ModerationSessionsOf(Session->GetClientGameInstance(1))->GetState() == ECKSessionsState::InSession
						&& HostWorld != nullptr && HostWorld->GetNetMode() == NM_ListenServer
						&& WorldOfB != nullptr && WorldOfB->GetNetMode() == NM_ListenServer;
				},
				ModerationTravelTicks });

			Steps.Add({ TEXT("Client A searches for a group of 1 and sees both sessions with their open slots"),
				[Session]()
				{
					ModerationSessionsOf(Session->GetClientGameInstance(0))->FindSessionsForGroup(nullptr, TMap<FName, FString>(), 1);
				},
				[TestPtr = &Test, Session, OpenSlots]()
				{
					const UCKSessionsSubsystem* Sessions = ModerationSessionsOf(Session->GetClientGameInstance(0));
					if (Sessions->GetState() != ECKSessionsState::Idle)
					{
						return false;
					}
					const TArray<FCKSessionsSearchResult>& Results = Sessions->GetSearchResults();
					TestPtr->TestEqual(TEXT("Both sessions are found"), Results.Num(), 2);
					int32 Smallest = MAX_int32;
					for (const FCKSessionsSearchResult& Result : Results)
					{
						Smallest = FMath::Min(Smallest, CKSessions::Online::GetOpenSlots(Result.GetNative()));
					}
					*OpenSlots = Results.Num() > 0 ? Smallest : 0;
					TestPtr->TestTrue(TEXT("A session has open slots"), *OpenSlots >= 1);
					return true;
				},
				ModerationSearchTicks });

			Steps.Add({ TEXT("Client A searches for a group one larger than any session has room for and finds none"),
				[Session, OpenSlots]()
				{
					int32 Largest = 0;
					for (const FCKSessionsSearchResult& Result : ModerationSessionsOf(Session->GetClientGameInstance(0))->GetSearchResults())
					{
						Largest = FMath::Max(Largest, CKSessions::Online::GetOpenSlots(Result.GetNative()));
					}
					*OpenSlots = Largest;
					ModerationSessionsOf(Session->GetClientGameInstance(0))->FindSessionsForGroup(nullptr, TMap<FName, FString>(), Largest + 1);
				},
				[TestPtr = &Test, Session]()
				{
					const UCKSessionsSubsystem* Sessions = ModerationSessionsOf(Session->GetClientGameInstance(0));
					if (Sessions->GetState() != ECKSessionsState::Idle)
					{
						return false;
					}
					TestPtr->TestEqual(TEXT("No session has room for the group"), Sessions->GetSearchResults().Num(), 0);
					return true;
				},
				ModerationSearchTicks });

			Steps.Add({ TEXT("Client A quick plays with a group that does not fit anywhere and hosts"),
				[Session, OpenSlots, TooBig]()
				{
					UCKSessionsSubsystem* Sessions = ModerationSessionsOf(Session->GetClientGameInstance(0));
					(*TooBig)->Watch(*Sessions);
					Sessions->QuickPlayForGroup(nullptr, TMap<FName, FString>(), GetModerationMap(), *OpenSlots + 1);
				},
				[TestPtr = &Test, Session, TooBig]()
				{
					const UCKSessionsTestQuickPlayRecorder* Record = TooBig->Get();
					if (!Record->IsCompleted())
					{
						return false;
					}
					TestPtr->TestTrue(TEXT("Quick play succeeded"), Record->Succeeded());
					TestPtr->TestTrue(TEXT("Quick play hosted instead of joining"), Record->Hosted());
					TestPtr->TestEqual(TEXT("The host did not get the group"), ModerationCountPlayersIn(Session->GetServerWorld()), 1);
					TestPtr->TestEqual(TEXT("Client B did not get the group"), ModerationCountPlayersIn(Session->GetClientWorld(1)), 1);
					return true;
				},
				ModerationSearchTicks + ModerationTravelTicks });

			Steps.Add({ TEXT("Client C quick plays with a group that fits the sessions and joins one"),
				[Session, OpenSlots, Fits]()
				{
					UCKSessionsSubsystem* Sessions = ModerationSessionsOf(Session->GetClientGameInstance(2));
					(*Fits)->Watch(*Sessions);
					Sessions->QuickPlayForGroup(nullptr, TMap<FName, FString>(), GetModerationMap(), *OpenSlots);
				},
				[TestPtr = &Test, Session, Fits]()
				{
					const UCKSessionsTestQuickPlayRecorder* Record = Fits->Get();
					if (!Record->IsCompleted())
					{
						return false;
					}
					TestPtr->TestTrue(TEXT("Quick play succeeded"), Record->Succeeded());
					TestPtr->TestFalse(TEXT("Quick play joined a session instead of hosting"), Record->Hosted());
					TestPtr->TestEqual(TEXT("The group found sessions with room"), ModerationSessionsOf(Session->GetClientGameInstance(2))->GetSearchResults().Num(), 3);
					return true;
				},
				ModerationSearchTicks + ModerationTravelTicks });

			return StartScenario(Test, Session, MoveTemp(Steps), TEXT("group quick play"));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsKickListenTest, "CoopKit.Sessions.Kick.ListenServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsKickListenTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunKickScenario(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsBanListenTest, "CoopKit.Sessions.Ban.ListenServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsBanListenTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunBanScenario(*this);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsQuickPlayGroupListenTest, "CoopKit.Sessions.QuickPlay.Group",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsQuickPlayGroupListenTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunGroupScenario(*this);
}

#endif // WITH_DEV_AUTOMATION_TESTS
