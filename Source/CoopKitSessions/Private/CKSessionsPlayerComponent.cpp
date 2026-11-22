// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsPlayerComponent.h"
#include "CKSessionsGameplayTags.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

UCKSessionsPlayerComponent::UCKSessionsPlayerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

UCKSessionsPlayerComponent* UCKSessionsPlayerComponent::FindPlayerComponent(const APlayerState* PlayerState)
{
	return PlayerState != nullptr ? PlayerState->FindComponentByClass<UCKSessionsPlayerComponent>() : nullptr;
}

UCKSessionsPlayerComponent* UCKSessionsPlayerComponent::FindLocalPlayerComponent(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const APlayerController* Controller = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	return Controller != nullptr ? FindPlayerComponent(Controller->PlayerState) : nullptr;
}

void UCKSessionsPlayerComponent::SetReady(const bool bInReady)
{
	if (GetOwner() == nullptr)
	{
		return;
	}
	if (GetOwner()->HasAuthority())
	{
		if (bReady != bInReady)
		{
			bReady = bInReady;
			GetOwner()->ForceNetUpdate();
			OnRep_Ready();
		}
		return;
	}
	ServerSetReady(bInReady);
}

void UCKSessionsPlayerComponent::ServerSetReady_Implementation(const bool bInReady)
{
	SetReady(bInReady);
}

void UCKSessionsPlayerComponent::OnRep_Ready()
{
	OnReadyChanged.Broadcast(this, bReady);
}

void UCKSessionsPlayerComponent::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.Reset();
	if (bReady)
	{
		TagContainer.AddTag(CKSessions::Tags::State_Ready);
	}
}

void UCKSessionsPlayerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCKSessionsPlayerComponent, bReady);
}
