// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "CKSessionsShowcaseGameState.generated.h"

class UCKSessionsLobbyComponent;
class UCKSessionsLobbyRulesData;

/**
 * Game state of the CoopKit Sessions overview map: the plugin's own lobby component with the rules of the map (the data
 * asset set on the Blueprint child), so the Lobby stand shows the real lobby, not a drawing of it.
 */
UCLASS(Blueprintable)
class ACKSessionsShowcaseGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ACKSessionsShowcaseGameState();

	virtual void PostInitializeComponents() override;

private:
	/** Lobby rules of the map; the lobby component takes them on every machine. */
	UPROPERTY(EditDefaultsOnly, Category = "CoopKit|Sessions")
	TObjectPtr<UCKSessionsLobbyRulesData> LobbyRules;

	UPROPERTY(VisibleAnywhere, Category = "CoopKit|Sessions")
	TObjectPtr<UCKSessionsLobbyComponent> Lobby;
};
