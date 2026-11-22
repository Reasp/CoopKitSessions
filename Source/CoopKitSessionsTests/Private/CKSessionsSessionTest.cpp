// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsLibrary.h"
#include "CKSessionsNetTestSession.h"
#include "CKSessionsSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CKSessions::Tests
{
	namespace
	{
		const TCHAR* DemoMap = TEXT("/CoopKitSessions/Maps/L_SessionsDemo");

		/** Search with the Null subsystem always takes 5 s (LAN_QUERY_TIMEOUT); 300 ticks at 30 fps leave room for a busy machine. */
		constexpr int32 SearchTicks = 300;

		/** Join: resolve the address, travel, log in, under 150 ms lag and 2% loss. Usually 1-2 s. */
		constexpr int32 JoinTicks = 300;

		/** Reconnect: 2 s pause, the engine's travel back to the default map, a 5 s search, the join. Usually about 10 s. */
		constexpr int32 ReconnectTicks = 750;

		UCKSessionsSubsystem* GetSessions(UGameInstance* GameInstance)
		{
			return GameInstance != nullptr ? GameInstance->GetSubsystem<UCKSessionsSubsystem>() : nullptr;
		}

		int32 CountServerPlayers(const FNetTestSession& Session)
		{
			const UWorld* World = Session.GetServerWorld();
			return World != nullptr && World->GetGameState() != nullptr ? World->GetGameState()->PlayerArray.Num() : -1;
		}

		int32 CountConnectedRecords(const UCKSessionsSubsystem& Sessions)
		{
			int32 Connected = 0;
			for (const FCKSessionsPlayerRecord& Record : Sessions.GetPlayerRecords())
			{
				Connected += Record.bConnected ? 1 : 0;
			}
			return Connected;
		}

		/** Local player state of a client instance, as the client sees it. */
		APlayerState* GetClientPlayerState(const FNetTestSession& Session, const int32 ClientIndex)
		{
			const UWorld* World = Session.GetClientWorld(ClientIndex);
			const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
			return Controller != nullptr ? Controller->PlayerState.Get() : nullptr;
		}

		/** Player controller of a client instance on the server, found by the client's unique net id. */
		APlayerController* FindServerController(const FNetTestSession& Session, const int32 ClientIndex)
		{
			const APlayerState* ClientState = GetClientPlayerState(Session, ClientIndex);
			UWorld* ServerWorld = Session.GetServerWorld();
			if (ClientState == nullptr || ServerWorld == nullptr)
			{
				return nullptr;
			}
			for (FConstPlayerControllerIterator It = ServerWorld->GetPlayerControllerIterator(); It; ++It)
			{
				APlayerController* Controller = It->Get();
				if (Controller != nullptr && Controller->PlayerState != nullptr && Controller->PlayerState->GetUniqueId() == ClientState->GetUniqueId())
				{
					return Controller;
				}
			}
			return nullptr;
		}

		/** Steps "find the session" and "join it" for one client. bInProgress: the search must report a running match. */
		void AddFindAndJoinSteps(TArray<FNetStep>& Steps, const TSharedRef<FNetTestSession>& Session, const int32 ClientIndex, const TCHAR* ClientName, const bool bInProgress)
		{
			Steps.Add({ FString::Printf(TEXT("Client %s finds the session%s"), ClientName, bInProgress ? TEXT(" with the match in progress") : TEXT("")),
				[Session, ClientIndex]()
				{
					GetSessions(Session->GetClientGameInstance(ClientIndex))->FindSessions(nullptr);
				},
				[Session, ClientIndex, bInProgress]()
				{
					const UCKSessionsSubsystem* Sessions = GetSessions(Session->GetClientGameInstance(ClientIndex));
					return Sessions->GetState() == ECKSessionsState::Idle && Sessions->GetSearchResults().Num() == 1
						&& UCKSessionsLibrary::IsMatchInProgress(Sessions->GetSearchResults()[0]) == bInProgress;
				},
				SearchTicks });

			Steps.Add({ FString::Printf(TEXT("Client %s joins and the server sees the player"), ClientName),
				[Session, ClientIndex]()
				{
					UCKSessionsSubsystem* Sessions = GetSessions(Session->GetClientGameInstance(ClientIndex));
					Sessions->JoinSession(Sessions->GetSearchResults()[0]);
				},
				[Session, ClientIndex]()
				{
					const bool bListen = Session->GetSettings().Host == ENetSessionHost::ListenServer;
					const int32 ExpectedPlayers = ClientIndex + 1 + (bListen ? 1 : 0);
					return GetSessions(Session->GetClientGameInstance(ClientIndex))->GetState() == ECKSessionsState::InSession
						&& CountServerPlayers(*Session) == ExpectedPlayers;
				},
				JoinTicks });
		}

		/**
		 * Host -> client A finds and joins -> the host starts the match -> client B finds the match in progress and joins
		 * -> a server change reaches B -> A loses the connection and reconnects. Every client joins through the plugin's
		 * API, with network emulation on every connection.
		 */
		bool RunSessionScenario(FAutomationTestBase& Test, const ENetSessionHost Host)
		{
			FNetSessionSettings Settings;
			Settings.Host = Host;
			Settings.ClientConnect = ENetClientConnect::Manual;
			Settings.RemoteClients = 2;
			const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(Settings);
			const bool bListen = Host == ENetSessionHost::ListenServer;
			const int32 HostPlayers = bListen ? 1 : 0;

			TArray<FNetStep> Steps;
			if (bListen)
			{
				Steps.Add({ TEXT("Host opens the map as a listen server and creates the session"),
					[Session]()
					{
						GetSessions(Session->GetHostGameInstance())->HostSession(nullptr, TSoftObjectPtr<UWorld>(FSoftObjectPath(FString(DemoMap) + TEXT(".L_SessionsDemo"))));
					},
					[Session]()
					{
						const UWorld* World = Session->GetServerWorld();
						return GetSessions(Session->GetHostGameInstance())->GetState() == ECKSessionsState::InSession
							&& World != nullptr && World->GetNetMode() == NM_ListenServer;
					},
					JoinTicks });
			}
			else
			{
				Steps.Add({ TEXT("Dedicated server creates the session by itself"),
					nullptr,
					[Session]()
					{
						return GetSessions(Session->GetHostGameInstance())->GetState() == ECKSessionsState::InSession;
					},
					60 });
			}

			AddFindAndJoinSteps(Steps, Session, 0, TEXT("A"), false);

			Steps.Add({ TEXT("Host starts the match"),
				[Session]()
				{
					GetSessions(Session->GetHostGameInstance())->StartMatch();
				},
				[Session]()
				{
					return GetSessions(Session->GetHostGameInstance())->IsMatchInProgress();
				},
				5 });

			AddFindAndJoinSteps(Steps, Session, 1, TEXT("B"), true);

			Steps.Add({ TEXT("Host's player records list every connected player"),
				nullptr,
				[Session, HostPlayers]()
				{
					return CountConnectedRecords(*GetSessions(Session->GetHostGameInstance())) == 2 + HostPlayers;
				},
				5 });

			// Replication to the late joiner, and proof that the emulated lag is on: data must not arrive sooner than the lag.
			TSharedRef<double> ChangeSentAt = MakeShared<double>(0.0);
			Steps.Add({ TEXT("A server change on client A's player state reaches client B"),
				[Session, ChangeSentAt]()
				{
					if (APlayerController* Controller = FindServerController(*Session, 0))
					{
						Controller->PlayerState->SetScore(42.0f);
						Controller->PlayerState->ForceNetUpdate();
					}
					*ChangeSentAt = FPlatformTime::Seconds();
				},
				[Session]()
				{
					const APlayerState* StateOfA = GetClientPlayerState(*Session, 0);
					const UWorld* WorldOfB = Session->GetClientWorld(1);
					if (StateOfA == nullptr || WorldOfB == nullptr || WorldOfB->GetGameState() == nullptr)
					{
						return false;
					}
					for (const APlayerState* State : WorldOfB->GetGameState()->PlayerArray)
					{
						if (State != nullptr && State->GetUniqueId() == StateOfA->GetUniqueId() && FMath::IsNearlyEqual(State->GetScore(), 42.0f))
						{
							return true;
						}
					}
					return false;
				},
				90 });
			Steps.Add({ TEXT("The change took at least the emulated lag"),
				nullptr,
				[TestPtr = &Test, Session, ChangeSentAt]()
				{
					const double ElapsedMs = (FPlatformTime::Seconds() - *ChangeSentAt) * 1000.0;
					if (ElapsedMs < Session->GetSettings().PacketLagMs * 0.9)
					{
						TestPtr->AddError(FString::Printf(TEXT("Data arrived in %.0f ms, faster than the %d ms lag: network emulation is not active."), ElapsedMs, Session->GetSettings().PacketLagMs));
					}
					return true;
				},
				1 });

			// A dropped link: both ends close the connection, so the drop does not depend on a close packet surviving the loss.
			Steps.Add({ TEXT("Client A loses the connection and starts reconnecting"),
				[Session]()
				{
					if (APlayerController* ServerController = FindServerController(*Session, 0))
					{
						if (UNetConnection* Connection = ServerController->GetNetConnection())
						{
							Connection->Close();
						}
					}
					const UWorld* WorldOfA = Session->GetClientWorld(0);
					UNetDriver* DriverOfA = WorldOfA != nullptr ? WorldOfA->GetNetDriver() : nullptr;
					if (DriverOfA != nullptr && DriverOfA->ServerConnection != nullptr)
					{
						DriverOfA->ServerConnection->Close();
					}
				},
				[Session]()
				{
					return GetSessions(Session->GetClientGameInstance(0))->GetState() == ECKSessionsState::Reconnecting;
				},
				90 });

			Steps.Add({ TEXT("Client A is back in the session and the host counts a reconnect"),
				nullptr,
				[Session, HostPlayers]()
				{
					const UCKSessionsSubsystem* HostSessions = GetSessions(Session->GetHostGameInstance());
					const bool bReconnectCounted = HostSessions->GetPlayerRecords().ContainsByPredicate([](const FCKSessionsPlayerRecord& Record)
					{
						return Record.bConnected && Record.ReconnectCount == 1;
					});
					return GetSessions(Session->GetClientGameInstance(0))->GetState() == ECKSessionsState::InSession
						&& bReconnectCounted && CountServerPlayers(*Session) == 2 + HostPlayers;
				},
				ReconnectTicks });

			// The dropped link is part of the scenario: the engine reports it once as an error and once as a warning.
			// Declared as expected, they also prove the drop happened (exactly one occurrence each).
			Test.AddExpectedErrorPlain(TEXT("FailureType = ConnectionLost"), EAutomationExpectedErrorFlags::Contains, 1);
			Test.AddExpectedMessagePlain(TEXT("GameNetDriver[ConnectionLost]"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);

			if (!Session->Start(Test, DemoMap))
			{
				return false;
			}
			Test.AddInfo(FString::Printf(TEXT("Scenario: %s, budgets: search %d, join %d, reconnect %d ticks"), *Session->Describe(), SearchTicks, JoinTicks, ReconnectTicks));
			Test.AddCommand(new FNetScenarioCommand(Test, Session, MoveTemp(Steps)));
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsSessionDedicatedTest, "CoopKit.Sessions.Session.DedicatedServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsSessionDedicatedTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunSessionScenario(*this, CKSessions::Tests::ENetSessionHost::DedicatedServer);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsSessionListenTest, "CoopKit.Sessions.Session.ListenServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsSessionListenTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunSessionScenario(*this, CKSessions::Tests::ENetSessionHost::ListenServer);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsRosterLateJoinTest, "CoopKit.Sessions.Roster.LateJoin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsRosterLateJoinTest::RunTest(const FString& Parameters)
{
	using namespace CKSessions::Tests;

	// PIE connects the clients itself; a third client joins later through PIE Late Join (the generic
	// "join in progress" step of the test harness). The dedicated server's records must follow.
	FNetSessionSettings Settings;
	Settings.Host = ENetSessionHost::DedicatedServer;
	Settings.RemoteClients = 2;
	const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(Settings);
	const TFunction<UCKSessionsSubsystem*()> HostSessions = [Session]()
	{
		const UWorld* World = Session->GetServerWorld();
		return World != nullptr ? GetSessions(World->GetGameInstance()) : nullptr;
	};

	TArray<FNetStep> Steps;
	Steps.Add({ TEXT("Dedicated server creates the session by itself"), nullptr,
		[HostSessions]() { return HostSessions() != nullptr && HostSessions()->GetState() == ECKSessionsState::InSession; }, 60 });
	Steps.Add({ TEXT("Host's records list both clients"), nullptr,
		[HostSessions]() { return CountConnectedRecords(*HostSessions()) == 2; }, 5 });
	Steps.Add({ TEXT("A third client joins the game in progress (PIE Late Join)"),
		[Session]() { Session->RequestLateJoin(); },
		[Session]() { return Session->AreAllPlayersConnected(); }, JoinTicks });
	Steps.Add({ TEXT("Host's records list the late joiner"), nullptr,
		[HostSessions]() { return CountConnectedRecords(*HostSessions()) == 3; }, 30 });

	if (!Session->Start(*this, DemoMap))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("Scenario: %s, then PIE Late Join"), *Session->Describe()));
	AddCommand(new FNetScenarioCommand(*this, Session, MoveTemp(Steps)));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
