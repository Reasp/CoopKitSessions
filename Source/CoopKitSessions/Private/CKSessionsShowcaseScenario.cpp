// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.
// CoopKit shared file: the same scenario base class ships in every CoopKit plugin under the plugin's prefix.

#include "CKSessionsShowcaseScenario.h"
#include "CoopKitSessionsModule.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

ACKSessionsShowcaseScenario::ACKSessionsShowcaseScenario()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ACKSessionsShowcaseScenario::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		GetWorldTimerManager().SetTimer(StepTimer, this, &ACKSessionsShowcaseScenario::AdvanceStep, StepSeconds, true, FirstStepDelaySeconds);
	}
}

void ACKSessionsShowcaseScenario::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(StepTimer);
	// Local players outlive the map: remove the bots when the map changes or the scenario is destroyed. When the game
	// or the play session ends, the game instance takes its local players down by itself.
	if (EndPlayReason == EEndPlayReason::Destroyed || EndPlayReason == EEndPlayReason::LevelTransition || EndPlayReason == EEndPlayReason::RemovedFromWorld)
	{
		for (APlayerController* Bot : Bots)
		{
			if (IsValid(Bot))
			{
				UGameplayStatics::RemovePlayer(Bot, true);
			}
		}
		if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
		{
			if (bDisabledSplitscreen)
			{
				Viewport->SetForceDisableSplitscreen(false);
			}
			if (RaisedPlayerLimitFrom != INDEX_NONE)
			{
				const UGameInstance* GameInstance = GetWorld()->GetGameInstance();
				Viewport->MaxSplitscreenPlayers = FMath::Max(RaisedPlayerLimitFrom, GameInstance != nullptr ? GameInstance->GetNumLocalPlayers() : 0);
			}
		}
	}
	Bots.Reset();
	Super::EndPlay(EndPlayReason);
}

void ACKSessionsShowcaseScenario::RunStep_Implementation(const int32 Step)
{
}

void ACKSessionsShowcaseScenario::AdvanceStep()
{
	const int32 Step = NextStep;
	NextStep = (NextStep + 1) % FMath::Max(StepCount, 1);
	RunStep(Step);
}

APlayerController* ACKSessionsShowcaseScenario::CreateBot(const FString& Name, const FVector& Location, const FRotator& Rotation, const TSubclassOf<APawn> PawnClass)
{
	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World != nullptr ? World->GetGameViewport() : nullptr;
	if (!HasAuthority() || Viewport == nullptr)
	{
		UE_LOG(LogCKSessions, Display, TEXT("%s: no bot %s - this world cannot have local players (dedicated server or no viewport)."), *GetName(), *Name);
		return nullptr;
	}
	// A bot is a local player without a view: keep the screen to the first player.
	if (!Viewport->IsSplitscreenForceDisabled())
	{
		Viewport->SetForceDisableSplitscreen(true);
		bDisabledSplitscreen = true;
	}
	// The viewport caps local players at its split-screen limit (4 by default); bots do not share the screen.
	const UGameInstance* GameInstance = World->GetGameInstance();
	const int32 Needed = (GameInstance != nullptr ? GameInstance->GetNumLocalPlayers() : 0) + 1;
	if (Viewport->MaxSplitscreenPlayers < Needed)
	{
		if (RaisedPlayerLimitFrom == INDEX_NONE)
		{
			RaisedPlayerLimitFrom = Viewport->MaxSplitscreenPlayers;
		}
		Viewport->MaxSplitscreenPlayers = Needed;
	}
	APlayerController* Bot = UGameplayStatics::CreatePlayer(this, -1, true);
	if (Bot == nullptr)
	{
		UE_LOG(LogCKSessions, Display, TEXT("%s: no bot %s - the game instance refused another local player."), *GetName(), *Name);
		return nullptr;
	}
	if (APlayerState* State = Bot->GetPlayerState<APlayerState>())
	{
		State->SetPlayerName(Name);
	}
	if (PawnClass != nullptr)
	{
		// The game mode gave the bot its default pawn on login: swap it for the scenario's own.
		if (APawn* DefaultPawn = Bot->GetPawn())
		{
			Bot->UnPossess();
			DefaultPawn->Destroy();
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (APawn* Pawn = World->SpawnActor<APawn>(PawnClass, Location, FRotator(0.0, Rotation.Yaw, 0.0), Params))
		{
			Bot->Possess(Pawn);
		}
	}
	PlaceBot(Bot, Location, Rotation);
	Bots.Add(Bot);
	return Bot;
}

void ACKSessionsShowcaseScenario::PlaceBot(APlayerController* Bot, const FVector& Location, const FRotator& Rotation)
{
	if (Bot == nullptr)
	{
		return;
	}
	Bot->SetControlRotation(Rotation);
	if (APawn* Pawn = Bot->GetPawn())
	{
		Pawn->SetActorLocationAndRotation(Location, FRotator(0.0, Rotation.Yaw, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void ACKSessionsShowcaseScenario::AimBot(APlayerController* Bot, const FVector& Target)
{
	if (Bot == nullptr)
	{
		return;
	}
	FVector Eyes;
	FRotator Unused;
	Bot->GetPlayerViewPoint(Eyes, Unused);
	const FRotator Look = (Target - Eyes).Rotation();
	Bot->SetControlRotation(Look);
	if (APawn* Pawn = Bot->GetPawn())
	{
		Pawn->SetActorRotation(FRotator(0.0, Look.Yaw, 0.0));
	}
}
