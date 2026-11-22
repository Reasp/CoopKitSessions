// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsShowcasePawn.h"
#include "CKSessionsPlayerComponent.h"
#include "GameFramework/PlayerState.h"

namespace CKSessions::Showcase::Pawn
{
	namespace
	{
		const FLinearColor ReadyColor(0.1f, 0.8f, 0.75f);
	}
}

void ACKSessionsShowcasePawn::GetLook(FCKSessionsShowcaseLook& OutLook) const
{
	using namespace CKSessions::Showcase::Pawn;

	Super::GetLook(OutLook);
	const UCKSessionsPlayerComponent* Player = UCKSessionsPlayerComponent::FindPlayerComponent(GetPlayerState());
	if (Player != nullptr && Player->IsReady())
	{
		OutLook.Color = ReadyColor;
		OutLook.Label += TEXT("\nREADY");
	}
}
