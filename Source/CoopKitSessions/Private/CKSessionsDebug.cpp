// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsDebug.h"
#include "CKSessionsLibrary.h"
#include "CKSessionsLobbyComponent.h"
#include "CKSessionsPlayerComponent.h"
#include "CKSessionsSubsystem.h"
#include "CoopKitSessionsModule.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace CKSessions::Debug
{
	namespace
	{
		TAutoConsoleVariable<int32> CVarDebug(
			TEXT("ck.sessions.Debug"),
			0,
			TEXT("1 shows the CoopKit Sessions overlay on every game viewport: session state, lobby phase, ready players, last search."),
			ECVF_Default);

		FDelegateHandle DrawHandle;

		/** Left edge and top of the text, in pixels: below the engine's own on-screen messages. */
		constexpr float TextLeft = 16.0f;
		constexpr float TextTop = 140.0f;

		void Draw(UCanvas* Canvas, APlayerController* Controller)
		{
			if (Canvas == nullptr || Controller == nullptr || CVarDebug.GetValueOnGameThread() == 0)
			{
				return;
			}
			const UFont* Font = GEngine->GetSmallFont();
			FFontRenderInfo RenderInfo;
			RenderInfo.bEnableShadow = true;

			float Y = TextTop;
			const TArray<FString> Lines = BuildOverlayLines(*Controller);
			for (int32 Index = 0; Index < Lines.Num(); ++Index)
			{
				Canvas->SetDrawColor(Index == 0 ? FColor(120, 200, 255) : FColor::White);
				Y += Canvas->DrawText(Font, Lines[Index], TextLeft, Y, 1.0f, 1.0f, RenderInfo);
			}
		}

		UCKSessionsSubsystem* GetSessions(const UWorld* World)
		{
			UCKSessionsSubsystem* Sessions = UCKSessionsSubsystem::Get(World);
			if (Sessions == nullptr)
			{
				UE_LOG(LogCKSessions, Warning, TEXT("No game instance in this world: run the command in a game (PIE or standalone)."));
			}
			return Sessions;
		}

		void Host(const TArray<FString>& Args, UWorld* World)
		{
			if (UCKSessionsSubsystem* Sessions = GetSessions(World))
			{
				const TSoftObjectPtr<UWorld> Map = Args.IsEmpty() ? TSoftObjectPtr<UWorld>() : TSoftObjectPtr<UWorld>(FSoftObjectPath(Args[0]));
				Sessions->HostSession(nullptr, Map);
			}
		}

		/** Key=Value arguments as session attributes; arguments without '=' are skipped with a warning. */
		TMap<FName, FString> ParseAttributeArgs(const TArray<FString>& Args)
		{
			TMap<FName, FString> Attributes;
			for (const FString& Arg : Args)
			{
				FString Key;
				FString Value;
				if (Arg.Split(TEXT("="), &Key, &Value) && !Key.IsEmpty())
				{
					Attributes.Add(FName(*Key), Value);
				}
				else
				{
					UE_LOG(LogCKSessions, Warning, TEXT("Skipped argument '%s': expected Key=Value."), *Arg);
				}
			}
			return Attributes;
		}

		void Find(const TArray<FString>& Args, UWorld* World)
		{
			if (UCKSessionsSubsystem* Sessions = GetSessions(World))
			{
				Sessions->FindSessionsWithAttributes(nullptr, ParseAttributeArgs(Args));
			}
		}

		void QuickPlay(const TArray<FString>& Args, UWorld* World)
		{
			if (UCKSessionsSubsystem* Sessions = GetSessions(World))
			{
				Sessions->QuickPlay(nullptr, ParseAttributeArgs(Args), TSoftObjectPtr<UWorld>());
			}
		}

		void Attribute(const TArray<FString>& Args, UWorld* World)
		{
			UCKSessionsSubsystem* Sessions = GetSessions(World);
			if (Sessions == nullptr)
			{
				return;
			}
			for (const TPair<FName, FString>& Pair : ParseAttributeArgs(Args))
			{
				Sessions->SetSessionAttribute(Pair.Key, Pair.Value);
			}
		}

		void Join(const TArray<FString>& Args, UWorld* World)
		{
			UCKSessionsSubsystem* Sessions = GetSessions(World);
			if (Sessions == nullptr)
			{
				return;
			}
			const int32 Index = Args.IsEmpty() ? 0 : FCString::Atoi(*Args[0]);
			if (!Sessions->GetSearchResults().IsValidIndex(Index))
			{
				UE_LOG(LogCKSessions, Warning, TEXT("ck.sessions.Join %d: no such search result (%d found); run ck.sessions.Find first."), Index, Sessions->GetSearchResults().Num());
				return;
			}
			Sessions->JoinSession(Sessions->GetSearchResults()[Index]);
		}

		void Leave(const TArray<FString>& Args, UWorld* World)
		{
			if (UCKSessionsSubsystem* Sessions = GetSessions(World))
			{
				Sessions->LeaveSession();
			}
		}

		void Ready(const TArray<FString>& Args, UWorld* World)
		{
			UCKSessionsPlayerComponent* Player = UCKSessionsPlayerComponent::FindLocalPlayerComponent(World);
			if (Player == nullptr)
			{
				UE_LOG(LogCKSessions, Warning, TEXT("ck.sessions.Ready: the local player state has no ready component (no lobby on this map?)."));
				return;
			}
			Player->SetReady(Args.IsEmpty() ? !Player->IsReady() : FCString::Atoi(*Args[0]) != 0);
		}

		void Start(const TArray<FString>& Args, UWorld* World)
		{
			UCKSessionsLobbyComponent* Lobby = UCKSessionsLobbyComponent::FindLobbyComponent(World);
			if (Lobby == nullptr || !Lobby->StartMatchNow())
			{
				UE_LOG(LogCKSessions, Warning, TEXT("ck.sessions.Start: run it on the server (the listen host's viewport) of a map with a lobby whose rules allow an early start."));
			}
		}

		FAutoConsoleCommandWithWorldAndArgs HostCommand(TEXT("ck.sessions.Host"),
			TEXT("Hosts a session with the default profile. Optional argument: map to open as a listen server, e.g. /CoopKitSessions/Maps/L_SessionsDemo."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Host));
		FAutoConsoleCommandWithWorldAndArgs FindCommand(TEXT("ck.sessions.Find"),
			TEXT("Searches for sessions of the default profile; ck.sessions.Debug 1 lists the results. Optional Key=Value arguments: only sessions with these attributes, e.g. ck.sessions.Find Mode=Survival."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Find));
		FAutoConsoleCommandWithWorldAndArgs QuickPlayCommand(TEXT("ck.sessions.QuickPlay"),
			TEXT("Joins a session of the default profile with the given Key=Value attributes, or hosts one on the current map when there is none."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&QuickPlay));
		FAutoConsoleCommandWithWorldAndArgs AttributeCommand(TEXT("ck.sessions.Attribute"),
			TEXT("Sets Key=Value attributes of the session this machine hosts (advertised at once while hosting, otherwise on the next host)."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Attribute));
		FAutoConsoleCommandWithWorldAndArgs JoinCommand(TEXT("ck.sessions.Join"),
			TEXT("Joins a session from the last search. Argument: result index, 0 by default."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Join));
		FAutoConsoleCommandWithWorldAndArgs LeaveCommand(TEXT("ck.sessions.Leave"),
			TEXT("Leaves the session and returns to the menu map."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Leave));
		FAutoConsoleCommandWithWorldAndArgs ReadyCommand(TEXT("ck.sessions.Ready"),
			TEXT("Toggles the local player's ready flag in the lobby. Optional argument: 1 ready, 0 not ready."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Ready));
		FAutoConsoleCommandWithWorldAndArgs StartCommand(TEXT("ck.sessions.Start"),
			TEXT("Server only: starts the match now if the lobby rules allow an early start."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Start));
	}

	TArray<FString> BuildOverlayLines(const APlayerController& Controller)
	{
		TArray<FString> Lines;
		const UWorld* World = Controller.GetWorld();
		const UCKSessionsSubsystem* Sessions = UCKSessionsSubsystem::Get(World);
		if (Sessions == nullptr)
		{
			return Lines;
		}
		Lines.Add(FString::Printf(TEXT("CoopKit Sessions: %s%s%s"), *StaticEnum<ECKSessionsState>()->GetNameStringByValue(static_cast<int64>(Sessions->GetState())),
			Sessions->IsHost() ? TEXT(", host") : TEXT(""), Sessions->IsMatchInProgress() ? TEXT(", match in progress") : TEXT("")));
		if (Sessions->GetLastFailure() != ECKSessionsFailure::None)
		{
			Lines.Add(FString::Printf(TEXT("Last failure: %s"), *StaticEnum<ECKSessionsFailure>()->GetNameStringByValue(static_cast<int64>(Sessions->GetLastFailure()))));
		}

		if (const UCKSessionsLobbyComponent* Lobby = UCKSessionsLobbyComponent::FindLobbyComponent(World))
		{
			FString LobbyLine = FString::Printf(TEXT("Lobby: %s, ready %d/%d"), *StaticEnum<ECKSessionsLobbyPhase>()->GetNameStringByValue(static_cast<int64>(Lobby->GetPhase())),
				Lobby->GetReadyPlayerCount(), Lobby->GetPlayerCount());
			if (Lobby->GetPhase() == ECKSessionsLobbyPhase::Countdown)
			{
				LobbyLine += FString::Printf(TEXT(", travel in %.1f s"), Lobby->GetCountdownSecondsLeft());
			}
			if (const UCKSessionsPlayerComponent* Player = UCKSessionsPlayerComponent::FindPlayerComponent(Controller.PlayerState))
			{
				LobbyLine += Player->IsReady() ? TEXT(", you are ready") : TEXT(", you are not ready (ck.sessions.Ready)");
			}
			Lines.Add(LobbyLine);
		}

		const TArray<FCKSessionsSearchResult>& Results = Sessions->GetSearchResults();
		for (int32 Index = 0; Index < Results.Num(); ++Index)
		{
			int32 Players = 0;
			int32 MaxPlayers = 0;
			UCKSessionsLibrary::GetPlayerCounts(Results[Index], Players, MaxPlayers);
			Lines.Add(FString::Printf(TEXT("  [%d] %s, %d/%d players, %d ms, %s%s"), Index, *UCKSessionsLibrary::GetHostName(Results[Index]), Players, MaxPlayers,
				UCKSessionsLibrary::GetPingMs(Results[Index]), *UCKSessionsLibrary::GetMapName(Results[Index]),
				UCKSessionsLibrary::IsMatchInProgress(Results[Index]) ? TEXT(", in progress") : TEXT("")));
		}
		return Lines;
	}

	void Register()
	{
#if !UE_BUILD_SHIPPING
		if (!DrawHandle.IsValid())
		{
			DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateStatic(&Draw));
		}
#endif
	}

	void Unregister()
	{
		if (DrawHandle.IsValid())
		{
			UDebugDrawService::Unregister(DrawHandle);
			DrawHandle.Reset();
		}
	}
}
