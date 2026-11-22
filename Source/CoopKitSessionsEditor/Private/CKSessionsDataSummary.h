// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace CKSessions::Editor
{
	/**
	 * Summary tab of a session profile: who can find and join the session, how reconnection behaves, and the exact
	 * online subsystem settings the profile produces with Null and with an online service.
	 */
	FText BuildProfileDataSummary(const UObject& Data);

	/** Summary tab of lobby rules: the order "waiting -> countdown -> travel" with the asset's numbers and map. */
	FText BuildLobbyRulesDataSummary(const UObject& Data);
}
