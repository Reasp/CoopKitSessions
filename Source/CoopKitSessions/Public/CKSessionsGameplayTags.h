// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 * Gameplay Tags of CoopKit Sessions (contract, see Docs/README.md > Integration). Published through
 * IGameplayTagAssetInterface of the lobby component (game state) and the player component (player state),
 * so other plugins read them without knowing this plugin.
 */
namespace CKSessions::Tags
{
	/** Game state: the lobby waits for players (also during the countdown). */
	COOPKITSESSIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Lobby);

	/** Game state: enough players are ready, the lobby counts down to the match. */
	COOPKITSESSIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Countdown);

	/** Game state: the match runs on the match map. */
	COOPKITSESSIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_InMatch);

	/** Player state: the player is ready in the lobby. */
	COOPKITSESSIONS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Ready);
}
