// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "CKSessionsTestTagComponent.generated.h"

/**
 * Test-only component that publishes a fixed set of Gameplay Tags, the way a plugin component publishes its state.
 * Lets the tests check the tag reading helpers without depending on any other plugin.
 */
UCLASS(NotBlueprintable, HideDropdown)
class UCKSessionsTestTagComponent final : public UActorComponent, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	/** Tags the component reports from GetOwnedGameplayTags. */
	void SetPublishedTags(const FGameplayTagContainer& InTags)
	{
		PublishedTags = InTags;
	}

	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override
	{
		// Resets the container on purpose: a reader must not lose tags collected from earlier sources.
		TagContainer = PublishedTags;
	}

private:
	FGameplayTagContainer PublishedTags;
};
