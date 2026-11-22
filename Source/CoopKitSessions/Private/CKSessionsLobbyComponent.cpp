// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsLobbyComponent.h"
#include "CKSessionsGameplayTags.h"
#include "CKSessionsLobbyRulesData.h"
#include "CKSessionsPlayerComponent.h"
#include "CKSessionsSettings.h"
#include "CKSessionsSubsystem.h"
#include "CoopKitSessionsModule.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "UObject/Package.h"

UCKSessionsLobbyComponent::UCKSessionsLobbyComponent()
{
	// The server checks the rules a few times per second; clients only read replicated values.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = 0.2f;
	SetIsReplicatedByDefault(true);
}

UCKSessionsLobbyComponent* UCKSessionsLobbyComponent::FindLobbyComponent(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
	return GameState != nullptr ? GameState->FindComponentByClass<UCKSessionsLobbyComponent>() : nullptr;
}

const UCKSessionsLobbyRulesData* UCKSessionsLobbyComponent::GetRules() const
{
	return Rules != nullptr ? Rules.Get() : UCKSessionsSettings::Get().LoadDefaultLobbyRules();
}

void UCKSessionsLobbyComponent::BeginPlay()
{
	Super::BeginPlay();

	const AActor* Owner = GetOwner();
	if (Owner == nullptr || !Owner->HasAuthority())
	{
		SetComponentTickEnabled(false);
		return;
	}

	// The same game state class often serves the lobby and the match map: on the match map the lobby is over.
	const UCKSessionsLobbyRulesData* LobbyRules = GetRules();
	const FString MapName = UWorld::RemovePIEPrefix(GetWorld()->GetPackage()->GetName());
	if (LobbyRules != nullptr && !LobbyRules->GetMatchMap().IsNull() && MapName == LobbyRules->GetMatchMap().GetLongPackageName())
	{
		SetPhase(ECKSessionsLobbyPhase::InMatch);
		SetComponentTickEnabled(false);
	}
}

void UCKSessionsLobbyComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (Phase == ECKSessionsLobbyPhase::Traveling || Phase == ECKSessionsLobbyPhase::InMatch)
	{
		return;
	}
	if (GetRules() == nullptr)
	{
		if (!bMissingRulesReported)
		{
			bMissingRulesReported = true;
			UE_LOG(LogCKSessions, Error, TEXT("%s has no lobby rules and the project has no default lobby rules: the lobby never starts."), *GetPathName());
		}
		return;
	}

	EnsurePlayerComponents();
	const bool bRulesMet = AreRulesMet();
	if (Phase == ECKSessionsLobbyPhase::Waiting && bRulesMet)
	{
		CountdownEndSeconds = GetServerSeconds() + GetRules()->GetCountdownSeconds();
		SetPhase(ECKSessionsLobbyPhase::Countdown);
	}
	else if (Phase == ECKSessionsLobbyPhase::Countdown && !bRulesMet)
	{
		CountdownEndSeconds = 0.0;
		SetPhase(ECKSessionsLobbyPhase::Waiting);
	}

	if (Phase == ECKSessionsLobbyPhase::Countdown && GetServerSeconds() >= CountdownEndSeconds)
	{
		TravelToMatch();
	}
}

double UCKSessionsLobbyComponent::GetServerSeconds() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
	return GameState != nullptr ? GameState->GetServerWorldTimeSeconds() : 0.0;
}

float UCKSessionsLobbyComponent::GetCountdownSecondsLeft() const
{
	return Phase == ECKSessionsLobbyPhase::Countdown ? static_cast<float>(FMath::Max(0.0, CountdownEndSeconds - GetServerSeconds())) : 0.0f;
}

int32 UCKSessionsLobbyComponent::GetPlayerCount() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
	if (GameState == nullptr)
	{
		return 0;
	}
	int32 Count = 0;
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		Count += PlayerState != nullptr && !PlayerState->IsOnlyASpectator() && !PlayerState->IsInactive() ? 1 : 0;
	}
	return Count;
}

int32 UCKSessionsLobbyComponent::GetReadyPlayerCount() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
	if (GameState == nullptr)
	{
		return 0;
	}
	int32 Count = 0;
	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		const UCKSessionsPlayerComponent* Player = UCKSessionsPlayerComponent::FindPlayerComponent(PlayerState);
		Count += Player != nullptr && Player->IsReady() && !PlayerState->IsOnlyASpectator() && !PlayerState->IsInactive() ? 1 : 0;
	}
	return Count;
}

bool UCKSessionsLobbyComponent::AreRulesMet() const
{
	const UCKSessionsLobbyRulesData* LobbyRules = GetRules();
	const int32 Players = GetPlayerCount();
	if (LobbyRules == nullptr || Players == 0 || Players < LobbyRules->GetMinPlayersToStart())
	{
		return false;
	}
	const int32 NeededReady = LobbyRules->RequiresAllReady() ? Players : LobbyRules->GetMinPlayersToStart();
	return GetReadyPlayerCount() >= NeededReady;
}

void UCKSessionsLobbyComponent::EnsurePlayerComponents()
{
	const UCKSessionsLobbyRulesData* LobbyRules = GetRules();
	const AGameStateBase* GameState = GetWorld() != nullptr ? GetWorld()->GetGameState() : nullptr;
	if (LobbyRules == nullptr || !LobbyRules->AddsPlayerComponents() || GameState == nullptr)
	{
		return;
	}
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (PlayerState != nullptr && UCKSessionsPlayerComponent::FindPlayerComponent(PlayerState) == nullptr)
		{
			// Added on the server; as a replicated component it reaches every client with the player state.
			UCKSessionsPlayerComponent* Player = NewObject<UCKSessionsPlayerComponent>(PlayerState, TEXT("CKSessionsPlayer"));
			Player->RegisterComponent();
		}
	}
}

bool UCKSessionsLobbyComponent::StartMatchNow()
{
	const UCKSessionsLobbyRulesData* LobbyRules = GetRules();
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority() || LobbyRules == nullptr
		|| (Phase != ECKSessionsLobbyPhase::Waiting && Phase != ECKSessionsLobbyPhase::Countdown))
	{
		return false;
	}
	if (!AreRulesMet() && !LobbyRules->CanHostForceStart())
	{
		return false;
	}
	TravelToMatch();
	return true;
}

void UCKSessionsLobbyComponent::TravelToMatch()
{
	const UCKSessionsLobbyRulesData* LobbyRules = GetRules();
	UWorld* World = GetWorld();
	if (LobbyRules == nullptr || World == nullptr || LobbyRules->GetMatchMap().IsNull())
	{
		UE_LOG(LogCKSessions, Error, TEXT("%s: the lobby rules have no match map; staying in the lobby."), *GetPathName());
		CountdownEndSeconds = 0.0;
		SetPhase(ECKSessionsLobbyPhase::Waiting);
		return;
	}

	SetPhase(ECKSessionsLobbyPhase::Traveling);
	if (UCKSessionsSubsystem* Sessions = UCKSessionsSubsystem::Get(this))
	{
		// Searches now show the match as in progress; without join in progress the session closes to newcomers.
		Sessions->StartMatch();
	}

	const FString Url = LobbyRules->GetMatchMap().GetLongPackageName() + LobbyRules->GetTravelOptions();
	const AGameModeBase* GameMode = World->GetAuthGameMode();
	if (GameMode != nullptr && !GameMode->bUseSeamlessTravel && !Url.Contains(TEXT("SeamlessTravel")))
	{
		UE_LOG(LogCKSessions, Warning, TEXT("%s does not use seamless travel: clients reload and reconnect to reach %s. Enable Use Seamless Travel on the game mode."),
			*GameMode->GetClass()->GetName(), *Url);
	}
	UE_LOG(LogCKSessions, Log, TEXT("Lobby travels to %s."), *Url);
	World->ServerTravel(Url, false);
}

void UCKSessionsLobbyComponent::SetPhase(const ECKSessionsLobbyPhase NewPhase)
{
	if (Phase != NewPhase)
	{
		Phase = NewPhase;
		if (GetOwner() != nullptr)
		{
			GetOwner()->ForceNetUpdate();
		}
		OnRep_Phase();
	}
}

void UCKSessionsLobbyComponent::OnRep_Phase()
{
	OnPhaseChanged.Broadcast(Phase);
}

void UCKSessionsLobbyComponent::GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const
{
	TagContainer.Reset();
	switch (Phase)
	{
	case ECKSessionsLobbyPhase::Countdown:
		TagContainer.AddTag(CKSessions::Tags::State_Lobby);
		TagContainer.AddTag(CKSessions::Tags::State_Countdown);
		break;
	case ECKSessionsLobbyPhase::InMatch:
		TagContainer.AddTag(CKSessions::Tags::State_InMatch);
		break;
	case ECKSessionsLobbyPhase::Waiting:
	case ECKSessionsLobbyPhase::Traveling:
	default:
		TagContainer.AddTag(CKSessions::Tags::State_Lobby);
		break;
	}
}

void UCKSessionsLobbyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCKSessionsLobbyComponent, Phase);
	DOREPLIFETIME(UCKSessionsLobbyComponent, CountdownEndSeconds);
}
