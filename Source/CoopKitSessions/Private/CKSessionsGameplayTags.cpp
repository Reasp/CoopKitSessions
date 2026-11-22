// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsGameplayTags.h"

namespace CKSessions::Tags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Lobby, "CK.Sessions.State.Lobby", "Game state: the lobby waits for players, also during the countdown. Set by CoopKit Sessions.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Countdown, "CK.Sessions.State.Countdown", "Game state: enough players are ready and the lobby counts down to the match. Set by CoopKit Sessions.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_InMatch, "CK.Sessions.State.InMatch", "Game state: the match runs on the match map. Set by CoopKit Sessions.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Ready, "CK.Sessions.State.Ready", "Player state: the player is ready in the lobby. Set by CoopKit Sessions.");
}
