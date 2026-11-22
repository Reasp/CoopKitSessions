// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsLobbyComponent.h"
#include "CKSessionsNetTestSession.h"
#include "CKSessionsShowcaseBoard.h"
#include "CKSessionsShowcaseStand.h"
#include "CKSessionsSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CKSessions::Tests
{
	namespace
	{
		const TCHAR* const OverviewMap = TEXT("/CoopKitSessions/Maps/L_SessionsOverview");

		template <typename TActor>
		int32 CountOverviewActors(UWorld* World)
		{
			int32 Count = 0;
			for (TActorIterator<TActor> It(World); It; ++It)
			{
				++Count;
			}
			return Count;
		}

		/** The scenario of the stand with the mode; null when the map has none. */
		const ACKSessionsShowcaseBoard* FindOverviewBoard(UWorld* World, const ECKSessionsShowcaseMode Mode)
		{
			if (World != nullptr)
			{
				for (TActorIterator<ACKSessionsShowcaseBoard> It(World); It; ++It)
				{
					if (It->GetMode() == Mode)
					{
						return *It;
					}
				}
			}
			return nullptr;
		}

		bool OverviewBoardHas(UWorld* World, const ECKSessionsShowcaseMode Mode, const TCHAR* Text)
		{
			const ACKSessionsShowcaseBoard* Board = FindOverviewBoard(World, Mode);
			return Board != nullptr && Board->GetBoardText().Contains(Text);
		}

		UWorld* FirstOverviewClient(const FNetTestSession& Session)
		{
			const TArray<UWorld*> Clients = Session.GetClientWorlds();
			return Clients.IsEmpty() ? nullptr : Clients[0];
		}

		/** The host's record of a player of the Roster stand; null when there is none. */
		bool FindOverviewRecord(UWorld* World, const TCHAR* Name, FCKSessionsPlayerRecord& OutRecord)
		{
			const UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
			const UCKSessionsSubsystem* Sessions = GameInstance != nullptr ? GameInstance->GetSubsystem<UCKSessionsSubsystem>() : nullptr;
			if (Sessions != nullptr)
			{
				for (const FCKSessionsPlayerRecord& Record : Sessions->GetPlayerRecords())
				{
					if (Record.PlayerName == Name)
					{
						OutRecord = Record;
						return true;
					}
				}
			}
			return false;
		}

		ECKSessionsLobbyPhase GetOverviewPhase(UWorld* World)
		{
			const UCKSessionsLobbyComponent* Lobby = UCKSessionsLobbyComponent::FindLobbyComponent(World);
			return Lobby != nullptr ? Lobby->GetPhase() : ECKSessionsLobbyPhase::InMatch;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsOverviewTest, "CoopKit.Sessions.Overview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsOverviewTest::RunTest(const FString& Parameters)
{
	using namespace CKSessions::Tests;

	FNetSessionSettings Settings;
	Settings.Host = ENetSessionHost::ListenServer;
	Settings.RemoteClients = 1;
	const TSharedRef<FNetTestSession> Session = MakeShared<FNetTestSession>(Settings);

	TArray<FNetStep> Steps;
	Steps.Add({ TEXT("The overview map has the welcome sign and four stands with their scenarios"), nullptr,
		[Session]()
		{
			UWorld* World = Session->GetServerWorld();
			return World != nullptr && CountOverviewActors<ACKSessionsShowcaseStand>(World) == 5 && CountOverviewActors<ACKSessionsShowcaseBoard>(World) == 4;
		}, 30 });

	Steps.Add({ TEXT("Stand 1: the board of the server shows the state of its sessions subsystem"), nullptr,
		[Session]()
		{
			return OverviewBoardHas(Session->GetServerWorld(), ECKSessionsShowcaseMode::Session, TEXT("State: Idle"));
		}, 300 });

	Steps.Add({ TEXT("Stand 2: the map has the plugin's lobby on the server and on the remote client"), nullptr,
		[Session]()
		{
			return UCKSessionsLobbyComponent::FindLobbyComponent(Session->GetServerWorld()) != nullptr
				&& UCKSessionsLobbyComponent::FindLobbyComponent(FirstOverviewClient(*Session)) != nullptr;
		}, 300 });

	Steps.Add({ TEXT("Stand 2: three ready bots start the countdown"), nullptr,
		[Session]()
		{
			UWorld* World = Session->GetServerWorld();
			const UCKSessionsLobbyComponent* Lobby = UCKSessionsLobbyComponent::FindLobbyComponent(World);
			return Lobby != nullptr && Lobby->GetPhase() == ECKSessionsLobbyPhase::Countdown && Lobby->GetReadyPlayerCount() >= 3
				&& OverviewBoardHas(World, ECKSessionsShowcaseMode::Lobby, TEXT("Phase: Countdown"));
		}, 900 });

	Steps.Add({ TEXT("Stand 2: the remote client sees the countdown on its own board"), nullptr,
		[Session]()
		{
			UWorld* Client = FirstOverviewClient(*Session);
			return GetOverviewPhase(Client) == ECKSessionsLobbyPhase::Countdown && OverviewBoardHas(Client, ECKSessionsShowcaseMode::Lobby, TEXT("Phase: Countdown"));
		}, 300 });

	Steps.Add({ TEXT("Stand 2: a bot cancels the countdown and the lobby stays on the overview map"), nullptr,
		[Session]()
		{
			UWorld* World = Session->GetServerWorld();
			return GetOverviewPhase(World) == ECKSessionsLobbyPhase::Waiting && World->GetMapName().Contains(TEXT("L_SessionsOverview"));
		}, 900 });

	Steps.Add({ TEXT("Stand 3: the host's record of Dan shows that he left and came back"), nullptr,
		[Session]()
		{
			FCKSessionsPlayerRecord Dan;
			UWorld* World = Session->GetServerWorld();
			return FindOverviewRecord(World, TEXT("Dan"), Dan) && Dan.ReconnectCount >= 1
				&& OverviewBoardHas(World, ECKSessionsShowcaseMode::Roster, TEXT("Dan: "))
				&& OverviewBoardHas(World, ECKSessionsShowcaseMode::Roster, TEXT("came back"));
		}, 900 });

	Steps.Add({ TEXT("Stand 3: the record of Eve, who stayed, has no returns"), nullptr,
		[Session]()
		{
			FCKSessionsPlayerRecord Eve;
			return FindOverviewRecord(Session->GetServerWorld(), TEXT("Eve"), Eve) && Eve.bConnected && Eve.ReconnectCount == 0;
		}, 300 });

	Steps.Add({ TEXT("Stand 4: the board lists the Blueprint nodes and the C++ classes"), nullptr,
		[Session]()
		{
			return OverviewBoardHas(Session->GetServerWorld(), ECKSessionsShowcaseMode::Api, TEXT("Join Session"))
				&& OverviewBoardHas(FirstOverviewClient(*Session), ECKSessionsShowcaseMode::Api, TEXT("UCKSessionsLobbyComponent"));
		}, 300 });

	if (!Session->Start(*this, OverviewMap))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("Scenario: %s"), *Session->Describe()));
	AddCommand(new FNetScenarioCommand(*this, Session, MoveTemp(Steps)));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
