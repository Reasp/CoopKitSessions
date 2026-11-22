// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class APlayerController;

/**
 * Console commands (ck.sessions.Host, Find, Join, Ready, Start, Leave) and the debug overlay (ck.sessions.Debug 1) of
 * CoopKit Sessions: drive and watch sessions and the lobby without any UI, in the demo map and with CoopKit NetTest.
 */
namespace CKSessions::Debug
{
	/** Starts drawing the overlay on game viewports; it stays hidden until ck.sessions.Debug is 1. No-op in Shipping. */
	void Register();

	/** Stops drawing the overlay. Safe to call when Register was not called. */
	void Unregister();

	/** Overlay text for the player's machine: session state, lobby phase and the last search. */
	TArray<FString> BuildOverlayLines(const APlayerController& Controller);
}
