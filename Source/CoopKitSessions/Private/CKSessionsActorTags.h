// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"

// Reading the state other plugins publish, without knowing which plugin set it.
//
// Every CoopKit plugin publishes its lasting state as Gameplay Tags through the engine's IGameplayTagAssetInterface,
// on the actor itself or on one of its components. A reader asks the actor in this order:
//   1. the actor, if it implements IGameplayTagAssetInterface;
//   2. every component of the actor that implements IGameplayTagAssetInterface;
//   3. the actor's Ability System Component, only in plugins that depend on GameplayAbilities:
//        if (const IAbilitySystemInterface* AbilityActor = Cast<IAbilitySystemInterface>(Actor))
//        {
//            if (const UAbilitySystemComponent* AbilitySystem = AbilityActor->GetAbilitySystemComponent())
//            {
//                AbilitySystem->GetOwnedGameplayTags(Tags);
//            }
//        }
// The helpers are header-only and private to the plugin: each plugin keeps its own copy, so plugins never link to
// each other.

namespace CKSessions::Tags
{
	namespace Private
	{
		/** Appends the tags of one source; implementations may reset the container they are given. */
		inline void AppendOwnedTags(const IGameplayTagAssetInterface& Source, FGameplayTagContainer& OutTags)
		{
			FGameplayTagContainer SourceTags;
			Source.GetOwnedGameplayTags(SourceTags);
			OutTags.AppendTags(SourceTags);
		}
	}

	/**
	 * Collects the Gameplay Tags an actor publishes: its own IGameplayTagAssetInterface, then every component that
	 * implements it. Works the same on the server and on clients when the tags come from replicated state.
	 * @param Actor    Actor to read; nullptr gives no tags.
	 * @param OutTags  Receives the tags; existing tags are kept.
	 */
	inline void GetActorTags(const AActor* Actor, FGameplayTagContainer& OutTags)
	{
		if (Actor == nullptr)
		{
			return;
		}

		if (const IGameplayTagAssetInterface* ActorTags = Cast<IGameplayTagAssetInterface>(Actor))
		{
			Private::AppendOwnedTags(*ActorTags, OutTags);
		}

		for (const UActorComponent* Component : Actor->GetComponentsByInterface(UGameplayTagAssetInterface::StaticClass()))
		{
			if (const IGameplayTagAssetInterface* ComponentTags = Cast<IGameplayTagAssetInterface>(Component))
			{
				Private::AppendOwnedTags(*ComponentTags, OutTags);
			}
		}
	}

	/** True when the actor publishes the tag or one of its child tags (CK.Revive.State matches CK.Revive.State.Downed). */
	inline bool HasActorTag(const AActor* Actor, const FGameplayTag Tag)
	{
		FGameplayTagContainer Tags;
		GetActorTags(Actor, Tags);
		return Tags.HasTag(Tag);
	}
}
