// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsGameplayTags.h"
#include "CKSessionsLobbyComponent.h"
#include "CKSessionsNetTestSession.h"
#include "CKSessionsPlayerComponent.h"
#include "CKSessionsSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CKSessions::Tests
{
	namespace
	{
		const TCHAR* LobbyMap = TEXT("/CoopKitSessions/Maps/L_SessionsDemo");
		const TCHAR* MatchMap = TEXT("/CoopKitSessions/Maps/L_SessionsMatch");

		/** Seamless travel is off in PIE unless this engine variable is set (GameModeBase::ProcessServerTravel, UE 5.4-5.8). */
		const TCHAR* AllowPieSeamlessTravel = TEXT("net.AllowPIESeamlessTravel");

		TAutoConsoleVariable<int32> CVarHardTravel(
			TEXT("ck.sessions.tests.HardTravel"),
			0,
			TEXT("Test only. 1 leaves seamless travel off in PIE, so the lobby tests must fail their seamless check. Proves the check detects a hard travel."),
			ECVF_Cheat);

		/** Restores net.AllowPIESeamlessTravel after the scenario, also after a failure. */
		class FRestoreSeamlessTravelCommand final : public IAutomationLatentCommand
		{
		public:
			explicit FRestoreSeamlessTravelCommand(const int32 InValue)
				: Value(InValue)
			{
			}

			virtual bool Update() override
			{
				if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(AllowPieSeamlessTravel))
				{
					Variable->Set(Value, ECVF_SetByCode);
				}
				return true;
			}

		private:
			int32 Value;
		};

		/**
		 * UE 5.4 workaround, tests only. After seamless travel in PIE, a client's GameplayDebugger local controller (outer:
		 * the player controller, which seamless travel keeps) draws through its replicator from the old world, already
		 * destroyed: UGameplayDebuggerLocalController::OnDebugDraw dereferences a null pointer and the editor crashes.
		 * UE 5.8 checks the pointer first. Marking those controllers as garbage makes the debug draw delegate skip them.
		 * Found by class name, so the tests do not link against GameplayDebugger.
		 */
		void SuppressGameplayDebuggerDraw()
		{
			const UClass* ControllerClass = FindObject<UClass>(nullptr, TEXT("/Script/GameplayDebugger.GameplayDebuggerLocalController"));
			if (ControllerClass == nullptr)
			{
				return;
			}
			TArray<UObject*> Controllers;
			GetObjectsOfClass(ControllerClass, Controllers);
			for (UObject* Controller : Controllers)
			{
				const UWorld* World = Controller->GetWorld();
				if (World != nullptr && World->WorldType == EWorldType::PIE && IsValid(Controller))
				{
					Controller->MarkAsGarbage();
				}
			}
		}

		FString GetMapName(const UWorld* World)
		{
			return World != nullptr ? UWorld::RemovePIEPrefix(World->GetPackage()->GetName()) : FString();
		}

		/** Every world of the session: the server first, then the clients. */
		TArray<UWorld*> GetAllWorlds(const FNetTestSession& Session)
		{
			TArray<UWorld*> Worlds;
			Worlds.Add(Session.GetServerWorld());
			Worlds.Append(Session.GetClientWorlds());
			return Worlds;
		}

		/** Ready component of the first local player of a world (the listen host's on the server world). */
		UCKSessionsPlayerComponent* GetLocalPlayer(const UWorld* World)
		{
			return World != nullptr ? UCKSessionsPlayerComponent::FindLocalPlayerComponent(World) : nullptr;
		}

		/** True when every machine sees every player of its game state with the given ready flag, and a ready component on each. */
		bool AllSeeReady(const FNetTestSession& Session, const int32 ExpectedReady)
		{
			for (const UWorld* World : GetAllWorlds(Session))
			{
				const UCKSessionsLobbyComponent* Lobby = UCKSessionsLobbyComponent::FindLobbyComponent(World);
				if (Lobby == nullptr || Lobby->GetPlayerCount() != Session.GetExpectedPlayerCount() || Lobby->GetReadyPlayerCount() != ExpectedReady)
				{
					return false;
				}
			}
			return true;
		}

		bool AllInPhase(const FNetTestSession& Session, const ECKSessionsLobbyPhase Phase)
		{
			for (const UWorld* World : GetAllWorlds(Session))
			{
				const UCKSessionsLobbyComponent* Lobby = UCKSessionsLobbyComponent::FindLobbyComponent(World);
				if (Lobby == nullptr || Lobby->GetPhase() != Phase)
				{
					return false;
				}
			}
			return true;
		}

		/** Sets the ready flag of every player: clients through their server RPC, a listen host directly on the server. */
		void SetEveryoneReady(const FNetTestSession& Session, const bool bReady)
		{
			for (const UWorld* World : GetAllWorlds(Session))
			{
				if (World != nullptr && World->GetNetMode() != NM_DedicatedServer)
				{
					if (UCKSessionsPlayerComponent* Player = GetLocalPlayer(World))
					{
						Player->SetReady(bReady);
					}
				}
			}
		}

		/** What the scenario remembers from before the travel, to prove the travel was seamless. */
		struct FBeforeTravel
		{
			TArray<TWeakObjectPtr<UNetDriver>> ClientDrivers;
		};

		bool RunLobbyScenario(FAutomationTestBase& Test, const ENetSessionHost Host)
		{
			FNetSessionSettings Settings;
			Settings.Host = Host;
			Settings.RemoteClients = 2;
			const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(Settings);
			const TSharedRef<FBeforeTravel> Before = MakeShared<FBeforeTravel>();
			const bool bDedicated = Host == ENetSessionHost::DedicatedServer;

			TArray<FNetStep> Steps;
			Steps.Add({ TEXT("Lobby reaches every machine, waiting, with a ready component per player"),
				nullptr,
				[Session]()
				{
					return AllInPhase(*Session, ECKSessionsLobbyPhase::Waiting) && AllSeeReady(*Session, 0)
						&& GetLocalPlayer(Session->GetClientWorlds()[0]) != nullptr && GetLocalPlayer(Session->GetClientWorlds()[1]) != nullptr;
				},
				90 });

			Steps.Add({ TEXT("Client A gets ready (server RPC) and every machine sees it"),
				[Session]() { GetLocalPlayer(Session->GetClientWorlds()[0])->SetReady(true); },
				[Session]() { return AllSeeReady(*Session, 1); },
				45 });

			Steps.Add({ TEXT("Everyone is ready: every machine counts down and sees the Countdown tag"),
				[Session]() { SetEveryoneReady(*Session, true); },
				[Session]()
				{
					SuppressGameplayDebuggerDraw();
					if (!AllInPhase(*Session, ECKSessionsLobbyPhase::Countdown))
					{
						return false;
					}
					FGameplayTagContainer Tags;
					UCKSessionsLobbyComponent::FindLobbyComponent(Session->GetClientWorlds()[1])->GetOwnedGameplayTags(Tags);
					return Tags.HasTagExact(CKSessions::Tags::State_Countdown)
						&& UCKSessionsLobbyComponent::FindLobbyComponent(Session->GetClientWorlds()[1])->GetCountdownSecondsLeft() > 0.0f;
				},
				45 });

			Steps.Add({ TEXT("Client B stops being ready: the countdown is cancelled everywhere"),
				[Session]() { GetLocalPlayer(Session->GetClientWorlds()[1])->SetReady(false); },
				[Session]() { SuppressGameplayDebuggerDraw(); return AllInPhase(*Session, ECKSessionsLobbyPhase::Waiting); },
				45 });

			Steps.Add({ TEXT("Client B is ready again: the countdown restarts"),
				[Session, Before]()
				{
					GetLocalPlayer(Session->GetClientWorlds()[1])->SetReady(true);
					for (const UWorld* World : Session->GetClientWorlds())
					{
						Before->ClientDrivers.Add(World->GetNetDriver());
					}
				},
				[Session]() { SuppressGameplayDebuggerDraw(); return AllInPhase(*Session, ECKSessionsLobbyPhase::Countdown); },
				45 });

			// Countdown 3 s (DA_SessionsDemoLobbyRules) plus the travel under lag.
			Steps.Add({ TEXT("The lobby travels every player to the match map, the lobby reports In Match"),
				nullptr,
				[Session]()
				{
					SuppressGameplayDebuggerDraw();
					for (const UWorld* World : GetAllWorlds(*Session))
					{
						const UCKSessionsLobbyComponent* Lobby = UCKSessionsLobbyComponent::FindLobbyComponent(World);
						if (GetMapName(World) != MatchMap || World->GetGameState() == nullptr || Lobby == nullptr
							|| Lobby->GetPhase() != ECKSessionsLobbyPhase::InMatch)
						{
							return false;
						}
					}
					return Session->AreAllPlayersConnected();
				},
				450 });

			Steps.Add({ TEXT("Travel was seamless: clients kept their connection, nobody logged in again"),
				nullptr,
				[TestPtr = &Test, Session, Before, bDedicated]()
				{
					const TArray<UWorld*> Clients = Session->GetClientWorlds();
					for (int32 Index = 0; Index < Clients.Num(); ++Index)
					{
						if (!Before->ClientDrivers.IsValidIndex(Index) || Before->ClientDrivers[Index].Get() != Clients[Index]->GetNetDriver())
						{
							TestPtr->AddError(TEXT("A client has a new net driver after travel: the travel was not seamless."));
							return true;
						}
					}
					const UCKSessionsSubsystem* HostSessions = UCKSessionsSubsystem::Get(Session->GetServerWorld());
					for (const FCKSessionsPlayerRecord& Record : HostSessions->GetPlayerRecords())
					{
						if (!Record.bConnected || Record.ReconnectCount != 0)
						{
							TestPtr->AddError(FString::Printf(TEXT("Player record %s changed during travel (connected %d, reconnects %d)."),
								*Record.PlayerName, Record.bConnected ? 1 : 0, Record.ReconnectCount));
						}
					}
					// The dedicated server hosts a session by itself, so the lobby marked its match as started.
					if (bDedicated && !HostSessions->IsMatchInProgress())
					{
						TestPtr->AddError(TEXT("The session's match is not marked as in progress after the lobby travelled."));
					}
					return true;
				},
				1 });

			IConsoleVariable* SeamlessVariable = IConsoleManager::Get().FindConsoleVariable(AllowPieSeamlessTravel);
			if (!Test.TestNotNull(TEXT("net.AllowPIESeamlessTravel exists"), SeamlessVariable))
			{
				return false;
			}
			const int32 SavedSeamless = SeamlessVariable->GetInt();
			const bool bHardTravel = CVarHardTravel.GetValueOnGameThread() != 0;
			if (bHardTravel)
			{
				Test.AddInfo(TEXT("ck.sessions.tests.HardTravel is set: PIE travels without seamless travel, this run must fail."));
			}
			SeamlessVariable->Set(bHardTravel ? 0 : 1, ECVF_SetByCode);
			if (!Session->Start(Test, LobbyMap))
			{
				SeamlessVariable->Set(SavedSeamless, ECVF_SetByCode);
				return false;
			}
			Test.AddInfo(FString::Printf(TEXT("Scenario: %s, lobby %s -> match %s, seamless travel"), *Session->Describe(), LobbyMap, MatchMap));
			Test.AddCommand(new FNetScenarioCommand(Test, Session, MoveTemp(Steps)));
			Test.AddCommand(new FRestoreSeamlessTravelCommand(SavedSeamless));
			return true;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsLobbyDedicatedTest, "CoopKit.Sessions.Lobby.DedicatedServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsLobbyDedicatedTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunLobbyScenario(*this, CKSessions::Tests::ENetSessionHost::DedicatedServer);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsLobbyListenTest, "CoopKit.Sessions.Lobby.ListenServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsLobbyListenTest::RunTest(const FString& Parameters)
{
	return CKSessions::Tests::RunLobbyScenario(*this, CKSessions::Tests::ENetSessionHost::ListenServer);
}

#endif // WITH_DEV_AUTOMATION_TESTS
