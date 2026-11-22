// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CKSessionsShowcaseMannequin.h"
#include "CoreMinimal.h"
#include "CKSessionsShowcasePawn.generated.h"

/**
 * Pawn of the CoopKit Sessions overview map: the showcase mannequin that shows its player's name, and READY in the
 * plugin's color once the player is ready in the lobby. The overview game mode spawns it for every player.
 */
UCLASS(NotPlaceable, NotBlueprintable)
class ACKSessionsShowcasePawn : public ACKSessionsShowcaseMannequin
{
	GENERATED_BODY()

protected:
	virtual void GetLook(FCKSessionsShowcaseLook& OutLook) const override;
};
