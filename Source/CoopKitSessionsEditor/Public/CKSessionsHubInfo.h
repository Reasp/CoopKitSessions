// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.
// CoopKit shared file: the same hub code ships in every CoopKit plugin with a hub, under the plugin's prefix. CoopKit shared group: hub.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CKSessionsHubInfo.generated.h"

/**
 * Marker of the CoopKit Hub contract version this plugin's copy of the hub implements. Every installed CoopKit plugin
 * has one; once all modules are loaded, the plugin with the highest version (ties: the smallest class path) raises the
 * shared CoopKitHub tab, the others only publish their entity types (decision-63 of the CoopKit line).
 */
UCLASS(meta = (CoopKitHubVersion = "2"))
class UCKSessionsHubInfo final : public UObject
{
	GENERATED_BODY()
};
