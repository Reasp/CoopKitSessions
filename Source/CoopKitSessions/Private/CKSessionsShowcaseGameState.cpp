// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsShowcaseGameState.h"
#include "CKSessionsLobbyComponent.h"
#include "CKSessionsLobbyRulesData.h"
#include "UObject/UnrealType.h"

ACKSessionsShowcaseGameState::ACKSessionsShowcaseGameState()
{
	Lobby = CreateDefaultSubobject<UCKSessionsLobbyComponent>(TEXT("Lobby"));
}

void ACKSessionsShowcaseGameState::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// The component's Rules property is private to the plugin's API (set in the editor): the map sets it by reflection.
	const FObjectProperty* RulesProperty = CastField<FObjectProperty>(UCKSessionsLobbyComponent::StaticClass()->FindPropertyByName(TEXT("Rules")));
	if (RulesProperty != nullptr && Lobby != nullptr && LobbyRules != nullptr)
	{
		RulesProperty->SetObjectPropertyValue_InContainer(Lobby, LobbyRules);
	}
}
