// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CKSessionsShowcaseGameMode.generated.h"

/**
 * Game mode of the CoopKit Sessions overview map: the showcase mannequin for every player, and the name of a bot set
 * while it logs in. The sessions subsystem keys its record of a player by the name the player logged in with, and a
 * local player logs in with the computer's name, so a bot that leaves and comes back has to carry the same name from
 * the start for the host to recognize it.
 */
UCLASS(Blueprintable)
class ACKSessionsShowcaseGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACKSessionsShowcaseGameMode();

	/** Name the next player to log in gets; empty for a real player. */
	void SetNextPlayerName(const FString& Name)
	{
		NextPlayerName = Name;
	}

	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;

private:
	FString NextPlayerName;
};
