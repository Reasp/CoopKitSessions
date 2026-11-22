// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsShowcaseGameMode.h"
#include "CKSessionsShowcaseGameState.h"
#include "CKSessionsShowcasePawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

ACKSessionsShowcaseGameMode::ACKSessionsShowcaseGameMode()
{
	DefaultPawnClass = ACKSessionsShowcasePawn::StaticClass();
	GameStateClass = ACKSessionsShowcaseGameState::StaticClass();
}

FString ACKSessionsShowcaseGameMode::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	const FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	if (!NextPlayerName.IsEmpty() && NewPlayerController != nullptr && NewPlayerController->PlayerState != nullptr)
	{
		NewPlayerController->PlayerState->SetPlayerName(NextPlayerName);
	}
	return Error;
}
