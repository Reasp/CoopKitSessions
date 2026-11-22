// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsLibrary.h"
#include "CKSessionsNetTestSession.h"
#include "CKSessionsSettings.h"
#include "CKSessionsSubsystem.h"
#include "CKSessionsTestHostLossRecorder.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameMapsSettings.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "Misc/AutomationTest.h"
#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CKSessions::Tests
{
	namespace
	{
		const TCHAR* HostLossMap = TEXT("/CoopKitSessions/Maps/L_SessionsDemo");
		const FName HostLossGroupKey(TEXT("Group"));

		/** One Null search (5 s) plus room for a busy machine, at the 30 fps cap of the session. */
		constexpr int32 HostLossSearchTicks = 300;

		/** Opening the map as a listen server and creating the session, or resolving, travelling and logging in. */
		constexpr int32 HostLossTravelTicks = 300;

		/**
		 * From the host's exit to On Host Lost on both clients: the 5 s connection timeout of a crash, the engine's travel
		 * to its default map. Usually under 6 s.
		 */
		constexpr int32 HostLossNoticeTicks = 450;

		/** The new host's session appears, and the other client's searches (5 s each) find it. Usually one or two searches. */
		constexpr int32 HostLossRegroupTicks = 900;

		/** Client connection timeout once the host is gone: Play In Editor turns timeouts off (UNetDriver::InitBase). */
		constexpr float HostLossClientTimeoutSeconds = 5.0f;

		enum class EHostExit : uint8
		{
			/** The host calls Leave Session: clients get FailureReceived ("Host closed the connection."). */
			Leaves,
			/** Every host packet is dropped and its online session is gone: clients find out by their timeout. */
			Crashes
		};

		using FRecorderRef = TSharedRef<TStrongObjectPtr<UCKSessionsTestHostLossRecorder>>;

		/** Clients: A and B hand a lost host over to the project, C keeps the plugin's default (the control). */
		constexpr int32 ClientA = 0;
		constexpr int32 ClientB = 1;
		constexpr int32 ClientC = 2;

		struct FHostLossState
		{
			TArray<FRecorderRef> Recorders;
			/** Inside On Host Lost: A's Host Session With Attributes and B's Find Sessions With Attributes were accepted. */
			bool bHostAccepted = false;
			bool bFindAccepted = false;
			/** Maps each client went through since the host's exit, one entry per new world (Play In Editor prefix removed). */
			TArray<TArray<FString>> MapsSinceExit;
			TArray<TWeakObjectPtr<UWorld>> LastWorlds;
			double HostExitAt = 0.0;
			/** Seconds from the host's exit to On Host Lost on A and B. */
			double NoticeSecondsA = -1.0;
			double NoticeSecondsB = -1.0;
			/** The control client C was seen reconnecting (crash only) and was sent to the menu. */
			bool bControlReconnected = false;
		};

		TSoftObjectPtr<UWorld> GetHostLossMap()
		{
			return TSoftObjectPtr<UWorld>(FSoftObjectPath(FString(HostLossMap) + TEXT(".L_SessionsDemo")));
		}

		UCKSessionsSubsystem* SessionsOfInstance(UGameInstance* GameInstance)
		{
			return GameInstance != nullptr ? GameInstance->GetSubsystem<UCKSessionsSubsystem>() : nullptr;
		}

		int32 CountPlayersInWorld(const UWorld* World)
		{
			return World != nullptr && World->GetGameState() != nullptr ? World->GetGameState()->PlayerArray.Num() : -1;
		}

		TMap<FName, FString> MakeGroup(const FString& Group)
		{
			TMap<FName, FString> Attributes;
			Attributes.Add(HostLossGroupKey, Group);
			return Attributes;
		}

		bool IsListenServerHost(UGameInstance* GameInstance, const UWorld* World)
		{
			const UCKSessionsSubsystem* Sessions = SessionsOfInstance(GameInstance);
			return Sessions != nullptr && Sessions->GetState() == ECKSessionsState::InSession && Sessions->IsHost()
				&& World != nullptr && World->GetNetMode() == NM_ListenServer;
		}

		FString GetDefaultMapPackage()
		{
			return FSoftObjectPath(UGameMapsSettings::GetGameDefaultMap()).GetLongPackageName();
		}

		/** Adds a new entry to MapsSinceExit for every client whose world changed since the last frame. */
		void TrackClientMaps(const FNetTestSession& Session, FHostLossState& State)
		{
			for (int32 Client = 0; Client < State.LastWorlds.Num(); ++Client)
			{
				UWorld* World = Session.GetClientWorld(Client);
				if (World != nullptr && World != State.LastWorlds[Client].Get())
				{
					State.LastWorlds[Client] = World;
					State.MapsSinceExit[Client].Add(UWorld::RemovePIEPrefix(World->GetPackage()->GetName()));
				}
			}
		}

		/** Checks that a client in the mode handed the lost host over: no reconnection, no menu, the right event. */
		void CheckHandedOver(FAutomationTestBase& Test, const UCKSessionsTestHostLossRecorder& Recorder, const TCHAR* Who,
			const ECKSessionsHostLossReason ExpectedReason)
		{
			Test.TestEqual(*FString::Printf(TEXT("%s: On Host Lost fired once"), Who), Recorder.GetHostLostCount(), 1);
			const FCKSessionsHostLoss& Loss = Recorder.GetHostLoss();
			Test.TestEqual(*FString::Printf(TEXT("%s: reason is %s"), Who, *UEnum::GetValueAsString(ExpectedReason)),
				UEnum::GetValueAsString(Loss.Reason), UEnum::GetValueAsString(ExpectedReason));
			Test.TestEqual(*FString::Printf(TEXT("%s: the lost session advertised Group = G1"), Who), Loss.Attributes.FindRef(HostLossGroupKey), FString(TEXT("G1")));
			Test.TestFalse(*FString::Printf(TEXT("%s: no reserved key among the attributes"), Who), Loss.Attributes.Contains(TEXT("CKS_TYPE")));
			Test.TestTrue(*FString::Printf(TEXT("%s: the event names the lost session"), Who),
				!Loss.SessionId.IsEmpty() && Loss.Session.IsValid() && Loss.Session.GetSessionId() == Loss.SessionId);
			Test.TestEqual(*FString::Printf(TEXT("%s: no On Session Failure"), Who), Recorder.GetSessionFailureCount(), 0);
			Test.TestEqual(*FString::Printf(TEXT("%s: Idle when the event fired"), Who),
				UEnum::GetValueAsString(Recorder.GetStateAtHostLost()), UEnum::GetValueAsString(ECKSessionsState::Idle));
			Test.TestTrue(*FString::Printf(TEXT("%s: standalone when the event fired"), Who), Recorder.GetNetModeAtHostLost() == NM_Standalone);
			Test.TestEqual(*FString::Printf(TEXT("%s: on the engine's default map when the event fired"), Who), Recorder.GetMapAtHostLost(), GetDefaultMapPackage());

			// From the loss to the event the subsystem only leaves: InSession -> Leaving -> Idle, no reconnect, no search.
			TArray<ECKSessionsState> Before = Recorder.GetStates();
			Before.SetNum(FMath::Max(0, Recorder.GetStatesBeforeHostLost()));
			const TArray<ECKSessionsState> Expected = { ECKSessionsState::Leaving, ECKSessionsState::Idle };
			FString Seen;
			for (const ECKSessionsState Entered : Before)
			{
				Seen += UEnum::GetValueAsString(Entered) + TEXT(" ");
			}
			Test.TestTrue(*FString::Printf(TEXT("%s: states before the event are Leaving, Idle (seen: %s)"), Who, *Seen), Before == Expected);
			Test.TestFalse(*FString::Printf(TEXT("%s: never reconnecting"), Who), Recorder.GetStates().Contains(ECKSessionsState::Reconnecting));
		}

		/**
		 * The host and three clients play a session with Group = G1. A and B hand a lost host over to the project, C keeps
		 * the plugin's default. The host leaves or crashes. A and B get On Host Lost on the engine's default map, without
		 * reconnection or the menu: A hosts the demo map with Group = G2 right inside the event, B searches Group = G2
		 * there, finds A and joins. C reacts the 1.0 way: On Session Failure (host left) or reconnection (crash).
		 */
		bool RunHostLossScenario(FAutomationTestBase& Test, const EHostExit Exit)
		{
			FNetSessionSettings Settings;
			Settings.Host = ENetSessionHost::ListenServer;
			Settings.ClientConnect = ENetClientConnect::Manual;
			Settings.RemoteClients = 3;
			const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(Settings);
			const TSharedRef<FHostLossState> State = MakeShared<FHostLossState>();
			for (int32 Client = 0; Client < Settings.RemoteClients; ++Client)
			{
				State->Recorders.Add(MakeShared<TStrongObjectPtr<UCKSessionsTestHostLossRecorder>>(NewObject<UCKSessionsTestHostLossRecorder>(GetTransientPackage())));
			}
			State->MapsSinceExit.SetNum(Settings.RemoteClients);
			State->LastWorlds.SetNum(Settings.RemoteClients);
			const ECKSessionsHostLossReason ExpectedReason = Exit == EHostExit::Leaves ? ECKSessionsHostLossReason::HostLeft : ECKSessionsHostLossReason::TimedOut;

			TArray<FNetStep> Steps;
			Steps.Add({ TEXT("Host hosts the demo map with Group = G1"),
				[Session]()
				{
					SessionsOfInstance(Session->GetHostGameInstance())->HostSessionWithAttributes(nullptr, GetHostLossMap(), MakeGroup(TEXT("G1")));
				},
				[Session]()
				{
					return IsListenServerHost(Session->GetHostGameInstance(), Session->GetServerWorld());
				},
				HostLossTravelTicks });

			Steps.Add({ TEXT("Clients A, B and C find the session by Group = G1"),
				[Session]()
				{
					for (int32 Client = 0; Client < 3; ++Client)
					{
						SessionsOfInstance(Session->GetClientGameInstance(Client))->FindSessionsWithAttributes(nullptr, MakeGroup(TEXT("G1")));
					}
				},
				[Session]()
				{
					for (int32 Client = 0; Client < 3; ++Client)
					{
						const UCKSessionsSubsystem* Sessions = SessionsOfInstance(Session->GetClientGameInstance(Client));
						if (Sessions->GetState() != ECKSessionsState::Idle || Sessions->GetSearchResults().Num() != 1)
						{
							return false;
						}
					}
					return true;
				},
				HostLossSearchTicks });

			Steps.Add({ TEXT("Clients A, B and C join; the host sees four players"),
				[Session]()
				{
					for (int32 Client = 0; Client < 3; ++Client)
					{
						UCKSessionsSubsystem* Sessions = SessionsOfInstance(Session->GetClientGameInstance(Client));
						Sessions->JoinSession(Sessions->GetSearchResults()[0]);
					}
				},
				[Session]()
				{
					for (int32 Client = 0; Client < 3; ++Client)
					{
						if (SessionsOfInstance(Session->GetClientGameInstance(Client))->GetState() != ECKSessionsState::InSession)
						{
							return false;
						}
					}
					return CountPlayersInWorld(Session->GetServerWorld()) == 4;
				},
				HostLossTravelTicks });

			Steps.Add({ TEXT("A and B hand a lost host over to the project (A will host, B will search); C keeps the default"),
				[TestPtr = &Test, Session, State]()
				{
					UCKSessionsSubsystem* SessionsA = SessionsOfInstance(Session->GetClientGameInstance(ClientA));
					UCKSessionsSubsystem* SessionsB = SessionsOfInstance(Session->GetClientGameInstance(ClientB));
					UCKSessionsSubsystem* SessionsC = SessionsOfInstance(Session->GetClientGameInstance(ClientC));
					TestPtr->TestFalse(TEXT("The mode is off by default"), SessionsA->IsHostLossHandledExternally());
					SessionsA->SetHandleHostLossExternally(true);
					SessionsB->SetHandleHostLossExternally(true);
					TestPtr->TestTrue(TEXT("A: the mode is on"), SessionsA->IsHostLossHandledExternally());
					TestPtr->TestTrue(TEXT("B: the mode is on"), SessionsB->IsHostLossHandledExternally());
					TestPtr->TestFalse(TEXT("C: the mode stays off"), SessionsC->IsHostLossHandledExternally());

					// Weak: the state owns the recorders, and the recorders own these reactions.
					const TWeakPtr<FHostLossState> WeakState = State;
					(*State->Recorders[ClientA])->Watch(*SessionsA, [WeakState](UCKSessionsSubsystem& Sessions, const FCKSessionsHostLoss&)
					{
						if (const TSharedPtr<FHostLossState> Pinned = WeakState.Pin())
						{
							Pinned->NoticeSecondsA = FPlatformTime::Seconds() - Pinned->HostExitAt;
							Pinned->bHostAccepted = Sessions.HostSessionWithAttributes(nullptr, GetHostLossMap(), MakeGroup(TEXT("G2")));
						}
					});
					(*State->Recorders[ClientB])->Watch(*SessionsB, [WeakState](UCKSessionsSubsystem& Sessions, const FCKSessionsHostLoss&)
					{
						if (const TSharedPtr<FHostLossState> Pinned = WeakState.Pin())
						{
							Pinned->NoticeSecondsB = FPlatformTime::Seconds() - Pinned->HostExitAt;
							Pinned->bFindAccepted = Sessions.FindSessionsWithAttributes(nullptr, MakeGroup(TEXT("G2")));
						}
					});
					(*State->Recorders[ClientC])->Watch(*SessionsC, nullptr);
				},
				[]() { return true; },
				2 });

			const TCHAR* ExitName = Exit == EHostExit::Leaves
				? TEXT("The host leaves the session (Leave Session)")
				: TEXT("The host crashes: no host packet gets through any more, its online session is gone");
			Steps.Add({ ExitName,
				[Session, State, Exit]()
				{
					// A packaged client times out by its net driver; Play In Editor never does. The host's close packet may
					// also be lost under the emulated loss, so the clients need the timeout when the host leaves too.
					for (int32 Client = 0; Client < 3; ++Client)
					{
						UWorld* World = Session->GetClientWorld(Client);
						if (UNetDriver* Driver = World != nullptr ? World->GetNetDriver() : nullptr)
						{
							Driver->bNoTimeouts = false;
							Driver->ConnectionTimeout = HostLossClientTimeoutSeconds;
							Driver->InitialConnectTimeout = HostLossClientTimeoutSeconds;
						}
						State->LastWorlds[Client] = World;
					}
					State->HostExitAt = FPlatformTime::Seconds();
					if (Exit == EHostExit::Leaves)
					{
						SessionsOfInstance(Session->GetHostGameInstance())->LeaveSession();
						return;
					}

					// The host world stays but goes silent. Leaving the map would not do: UNetConnection::Close flushes the
					// close packet past the loss emulation, and the clients would know at once.
					UWorld* HostWorld = Session->GetServerWorld();
#if DO_ENABLE_NET_TEST
					if (UNetDriver* HostDriver = HostWorld != nullptr ? HostWorld->GetNetDriver() : nullptr)
					{
						FPacketSimulationSettings Silence = HostDriver->PacketSimulationSettings;
						Silence.PktLoss = 100;
						HostDriver->SetPacketSimulationSettings(Silence);
					}
#endif
					// The session goes away the way a crashed host's does: no more LAN answers.
					const IOnlineSubsystem* HostOnline = HostWorld != nullptr ? Online::GetSubsystem(HostWorld) : nullptr;
					const IOnlineSessionPtr HostSessions = HostOnline != nullptr ? HostOnline->GetSessionInterface() : nullptr;
					if (HostSessions.IsValid())
					{
						HostSessions->DestroySession(NAME_GameSession);
					}
				},
				[]() { return true; },
				2 });

			Steps.Add({ TEXT("A and B get On Host Lost on the engine's default map; A hosts and B searches inside the event"), nullptr,
				[Session, State, Exit]()
				{
					TrackClientMaps(*Session, *State);
					// Crash: without the mode, C goes looking for the old session. Seen once, it is sent to the menu so its
					// attempts do not run under the rest of the scenario.
					UCKSessionsSubsystem* SessionsC = SessionsOfInstance(Session->GetClientGameInstance(ClientC));
					if (Exit == EHostExit::Crashes && !State->bControlReconnected && SessionsC->GetState() == ECKSessionsState::Reconnecting)
					{
						State->bControlReconnected = true;
						SessionsC->LeaveSession();
					}
					return (*State->Recorders[ClientA])->GetHostLostCount() > 0 && (*State->Recorders[ClientB])->GetHostLostCount() > 0;
				},
				HostLossNoticeTicks });

			Steps.Add({ TEXT("A and B handed the lost host over: right reason, Group = G1, no reconnection, no menu"), nullptr,
				[TestPtr = &Test, Session, State, ExpectedReason]()
				{
					CheckHandedOver(*TestPtr, **State->Recorders[ClientA], TEXT("Client A"), ExpectedReason);
					CheckHandedOver(*TestPtr, **State->Recorders[ClientB], TEXT("Client B"), ExpectedReason);
					TestPtr->TestTrue(TEXT("A: Host Session With Attributes accepted inside On Host Lost"), State->bHostAccepted);
					TestPtr->TestTrue(TEXT("B: Find Sessions With Attributes accepted inside On Host Lost"), State->bFindAccepted);
					TestPtr->AddInfo(FString::Printf(TEXT("On Host Lost %.2f s (A) and %.2f s (B) after the host's exit"), State->NoticeSecondsA, State->NoticeSecondsB));
					return true;
				},
				1 });

			Steps.Add({ TEXT("A hosts the demo map with Group = G2 and B finds it by the attribute"), nullptr,
				[Session, State]()
				{
					TrackClientMaps(*Session, *State);
					if (!IsListenServerHost(Session->GetClientGameInstance(ClientA), Session->GetClientWorld(ClientA)))
					{
						return false;
					}
					UCKSessionsSubsystem* SessionsB = SessionsOfInstance(Session->GetClientGameInstance(ClientB));
					if (SessionsB->GetState() != ECKSessionsState::Idle)
					{
						return false;
					}
					if (SessionsB->GetSearchResults().Num() == 1)
					{
						return true;
					}
					// The first search may have started before A's session existed: search again.
					SessionsB->FindSessionsWithAttributes(nullptr, MakeGroup(TEXT("G2")));
					return false;
				},
				HostLossRegroupTicks });

			Steps.Add({ TEXT("B joins A; A's game has two players"),
				[Session]()
				{
					UCKSessionsSubsystem* SessionsB = SessionsOfInstance(Session->GetClientGameInstance(ClientB));
					SessionsB->JoinSession(SessionsB->GetSearchResults()[0]);
				},
				[Session, State]()
				{
					TrackClientMaps(*Session, *State);
					return SessionsOfInstance(Session->GetClientGameInstance(ClientB))->GetState() == ECKSessionsState::InSession
						&& CountPlayersInWorld(Session->GetClientWorld(ClientA)) == 2;
				},
				HostLossTravelTicks });

			Steps.Add({ TEXT("A and B went from the engine's default map straight to the new game; C kept the 1.0 behavior"), nullptr,
				[TestPtr = &Test, Session, State, Exit]()
				{
					const FString DefaultMap = GetDefaultMapPackage();
					const TArray<FString> Expected = { DefaultMap, FString(HostLossMap) };
					for (const int32 Client : { ClientA, ClientB })
					{
						const FString Seen = FString::Join(State->MapsSinceExit[Client], TEXT(", "));
						TestPtr->TestTrue(*FString::Printf(TEXT("Client %s: maps since the host's exit are %s, %s (seen: %s)"),
							Client == ClientA ? TEXT("A") : TEXT("B"), *DefaultMap, HostLossMap, *Seen), State->MapsSinceExit[Client] == Expected);
					}
					const UCKSessionsTestHostLossRecorder& RecorderC = **State->Recorders[ClientC];
					TestPtr->TestEqual(TEXT("Client C: no On Host Lost without the mode"), RecorderC.GetHostLostCount(), 0);
					if (Exit == EHostExit::Leaves)
					{
						TestPtr->TestEqual(TEXT("Client C: On Session Failure, as in 1.0"), RecorderC.GetSessionFailureCount(), 1);
					}
					else
					{
						TestPtr->TestTrue(TEXT("Client C: reconnects to the old session, as in 1.0"), State->bControlReconnected);
					}
					return true;
				},
				1 });

			// The lost host is part of the scenario: the engine reports every client's network failure as an error
			// (FailureReceived when the host leaves, ConnectionTimeout when it crashes). The reason checks above tell them apart.
			Test.AddExpectedErrorPlain(TEXT("UEngine::BroadcastNetworkFailure: FailureType = "), EAutomationExpectedErrorFlags::Contains, 0);

			if (!Session->Start(Test, HostLossMap))
			{
				return false;
			}
			Test.AddInfo(FString::Printf(TEXT("Scenario: %s, host %s, budgets: search %d, travel %d, notice %d, regroup %d ticks"), *Session->Describe(),
				Exit == EHostExit::Leaves ? TEXT("leaves") : TEXT("crashes"), HostLossSearchTicks, HostLossTravelTicks, HostLossNoticeTicks, HostLossRegroupTicks));
			Test.AddCommand(new FNetScenarioCommand(Test, Session, MoveTemp(Steps)));
			return true;
		}
	}

	namespace
	{
		const TCHAR* MenuMapPackage = TEXT("/CoopKitSessions/Maps/L_SessionsOverview");

		/** Sets the Menu Map of the project settings in memory (the property is private); returns the previous value. */
		TSoftObjectPtr<UWorld> SwapMenuMap(const TSoftObjectPtr<UWorld>& NewMap)
		{
			UCKSessionsSettings* Settings = GetMutableDefault<UCKSessionsSettings>();
			const FProperty* Property = UCKSessionsSettings::StaticClass()->FindPropertyByName(TEXT("MenuMap"));
			TSoftObjectPtr<UWorld> Previous;
			if (const FSoftObjectProperty* SoftProperty = CastField<FSoftObjectProperty>(Property))
			{
				Previous = TSoftObjectPtr<UWorld>(FSoftObjectPath(SoftProperty->GetPropertyValue_InContainer(Settings).GetUniqueID()));
				SoftProperty->SetPropertyValue_InContainer(Settings, FSoftObjectPtr(NewMap.ToSoftObjectPath()));
			}
			return Previous;
		}

		struct FMenuMapState
		{
			TStrongObjectPtr<UCKSessionsTestHostLossRecorder> Recorder;
			TArray<FString> MapsSinceExit;
			TWeakObjectPtr<UWorld> LastWorld;
			TSoftObjectPtr<UWorld> PreviousMenuMap;
			bool bMenuMapSwapped = false;

			~FMenuMapState()
			{
				if (bMenuMapSwapped)
				{
					SwapMenuMap(PreviousMenuMap);
				}
			}
		};

		/**
		 * The plugin's 1.0 reaction to a lost host (no external handling) with a Menu Map that differs from the engine's
		 * default map: the client must end on the Menu Map, not on the default map the engine travels to by itself.
		 */
		bool RunMenuMapDiffersScenario(FAutomationTestBase& Test)
		{
			FNetSessionSettings Settings;
			Settings.Host = ENetSessionHost::ListenServer;
			Settings.ClientConnect = ENetClientConnect::Manual;
			Settings.RemoteClients = 1;
			const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(Settings);
			const TSharedRef<FMenuMapState> State = MakeShared<FMenuMapState>();
			State->Recorder.Reset(NewObject<UCKSessionsTestHostLossRecorder>(GetTransientPackage()));
			const FString MenuMapName(MenuMapPackage);

			TArray<FNetStep> Steps;
			Steps.Add({ TEXT("Host hosts the demo map"),
				[Session]()
				{
					SessionsOfInstance(Session->GetHostGameInstance())->HostSessionWithAttributes(nullptr, GetHostLossMap(), MakeGroup(TEXT("G1")));
				},
				[Session]()
				{
					return IsListenServerHost(Session->GetHostGameInstance(), Session->GetServerWorld());
				},
				HostLossTravelTicks });

			Steps.Add({ TEXT("The client finds the session"),
				[Session]()
				{
					SessionsOfInstance(Session->GetClientGameInstance(0))->FindSessionsWithAttributes(nullptr, MakeGroup(TEXT("G1")));
				},
				[Session]()
				{
					const UCKSessionsSubsystem* Sessions = SessionsOfInstance(Session->GetClientGameInstance(0));
					return Sessions->GetState() == ECKSessionsState::Idle && Sessions->GetSearchResults().Num() == 1;
				},
				HostLossSearchTicks });

			Steps.Add({ TEXT("The client joins"),
				[Session]()
				{
					UCKSessionsSubsystem* Sessions = SessionsOfInstance(Session->GetClientGameInstance(0));
					Sessions->JoinSession(Sessions->GetSearchResults()[0]);
				},
				[Session]()
				{
					return SessionsOfInstance(Session->GetClientGameInstance(0))->GetState() == ECKSessionsState::InSession
						&& CountPlayersInWorld(Session->GetServerWorld()) == 2;
				},
				HostLossTravelTicks });

			Steps.Add({ TEXT("The Menu Map differs from the default map; the client keeps the plugin's default reaction; the host leaves"),
				[TestPtr = &Test, Session, State, MenuMapName]()
				{
					const TSoftObjectPtr<UWorld> Overview{ FSoftObjectPath(MenuMapName + TEXT(".L_SessionsOverview")) };
					State->PreviousMenuMap = SwapMenuMap(Overview);
					State->bMenuMapSwapped = true;
					TestPtr->TestTrue(TEXT("The Menu Map differs from the engine's default map"), GetDefaultMapPackage() != MenuMapName);
					UCKSessionsSubsystem* Sessions = SessionsOfInstance(Session->GetClientGameInstance(0));
					TestPtr->TestFalse(TEXT("The client does not hand a lost host over"), Sessions->IsHostLossHandledExternally());
					State->Recorder->Watch(*Sessions, nullptr);

					UWorld* World = Session->GetClientWorld(0);
					if (UNetDriver* Driver = World != nullptr ? World->GetNetDriver() : nullptr)
					{
						Driver->bNoTimeouts = false;
						Driver->ConnectionTimeout = HostLossClientTimeoutSeconds;
						Driver->InitialConnectTimeout = HostLossClientTimeoutSeconds;
					}
					State->LastWorld = World;
					SessionsOfInstance(Session->GetHostGameInstance())->LeaveSession();
				},
				[]() { return true; },
				2 });

			Steps.Add({ TEXT("The client got On Session Failure and ends on the Menu Map"), nullptr,
				[Session, State, MenuMapName]()
				{
					UWorld* World = Session->GetClientWorld(0);
					if (World != nullptr && World != State->LastWorld.Get())
					{
						State->LastWorld = World;
						State->MapsSinceExit.Add(UWorld::RemovePIEPrefix(World->GetPackage()->GetName()));
					}
					return State->Recorder->GetSessionFailureCount() > 0 && State->MapsSinceExit.Num() > 0
						&& State->MapsSinceExit.Last() == MenuMapName && SessionsOfInstance(Session->GetClientGameInstance(0))->GetState() == ECKSessionsState::Idle;
				},
				HostLossNoticeTicks });

			Steps.Add({ TEXT("The client stays on the Menu Map"), nullptr,
				[TestPtr = &Test, Session, State, MenuMapName]()
				{
					const UWorld* World = Session->GetClientWorld(0);
					const FString Current = World != nullptr ? UWorld::RemovePIEPrefix(World->GetPackage()->GetName()) : FString();
					TestPtr->TestEqual(*FString::Printf(TEXT("The client is on the Menu Map (maps since the exit: %s)"), *FString::Join(State->MapsSinceExit, TEXT(", "))),
						Current, MenuMapName);
					return true;
				},
				1 });

			Test.AddExpectedErrorPlain(TEXT("UEngine::BroadcastNetworkFailure: FailureType = "), EAutomationExpectedErrorFlags::Contains, 0);
			if (!Session->Start(Test, HostLossMap))
			{
				return false;
			}
			Test.AddCommand(new FNetScenarioCommand(Test, Session, MoveTemp(Steps)));
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsHostLossLeavesTest, "CoopKit.Sessions.HostLoss.HostLeaves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsHostLossLeavesTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunHostLossScenario(*this, CKSessions::Tests::EHostExit::Leaves);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsHostLossCrashTest, "CoopKit.Sessions.HostLoss.HostCrash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsHostLossCrashTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunHostLossScenario(*this, CKSessions::Tests::EHostExit::Crashes);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsHostLossMenuMapTest, "CoopKit.Sessions.HostLoss.MenuMapDiffers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsHostLossMenuMapTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunMenuMapDiffersScenario(*this);
}

#endif // WITH_DEV_AUTOMATION_TESTS
