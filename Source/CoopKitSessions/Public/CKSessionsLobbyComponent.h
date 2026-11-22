// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "GameplayTagAssetInterface.h"
#include "CKSessionsLobbyComponent.generated.h"

class UCKSessionsLobbyRulesData;

/** Where the lobby is in its "waiting -> countdown -> travel" order. */
UENUM(BlueprintType)
enum class ECKSessionsLobbyPhase : uint8
{
	/** Waiting for enough ready players. */
	Waiting,
	/** Enough players are ready; the lobby travels when the countdown ends. */
	Countdown,
	/** The server travels every player to the match map. */
	Traveling,
	/** The current map is the match map of the rules: the lobby is over. */
	InMatch
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCKSessionsOnLobbyPhaseChanged, ECKSessionsLobbyPhase, Phase);

/**
 * The lobby of a co-op session, on the game state. The server counts ready players (UCKSessionsPlayerComponent on
 * each player state), counts down once the lobby rules are met, then travels everyone to the match map and marks
 * the session's match as started. Seamless when the game mode uses seamless travel.
 * Put it on the game state of the lobby map; on the match map (same game state class) it reports In Match.
 */
UCLASS(ClassGroup = (CoopKit), meta = (BlueprintSpawnableComponent))
class COOPKITSESSIONS_API UCKSessionsLobbyComponent final : public UActorComponent, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	UCKSessionsLobbyComponent();

	/** Lobby component of the world's game state; null when the world has none (or before the game state replicates). */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions", meta = (WorldContext = "WorldContextObject"))
	static UCKSessionsLobbyComponent* FindLobbyComponent(const UObject* WorldContextObject);

	/** Rules of this lobby: the component's own, else the project's default lobby rules; may be null. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	const UCKSessionsLobbyRulesData* GetRules() const;

	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	ECKSessionsLobbyPhase GetPhase() const
	{
		return Phase;
	}

	/** Seconds left in the countdown, on any machine (server clock); 0 outside the countdown. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	float GetCountdownSecondsLeft() const;

	/** Players in the lobby (spectators and players who left excluded). */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	int32 GetPlayerCount() const;

	/** Players in the lobby who are ready. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Sessions")
	int32 GetReadyPlayerCount() const;

	/**
	 * Server only: travels to the match now. Allowed when the rules are met, or when the rules let the host start early.
	 * A listen server host calls it directly; on a dedicated server, call it from your own server logic.
	 * @return False on a client, outside the lobby, or when the rules do not allow it.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions")
	bool StartMatchNow();

	/** Fires on every machine when the phase changes (server: at once; clients: on replication). */
	UPROPERTY(BlueprintAssignable, Category = "CoopKit|Sessions")
	FCKSessionsOnLobbyPhaseChanged OnPhaseChanged;

	// IGameplayTagAssetInterface: CK.Sessions.State.Lobby (+ .Countdown) in the lobby, CK.Sessions.State.InMatch on the match map.
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	bool AreRulesMet() const;
	void EnsurePlayerComponents();
	void SetPhase(ECKSessionsLobbyPhase NewPhase);
	void TravelToMatch();
	double GetServerSeconds() const;

	UFUNCTION()
	void OnRep_Phase();

	/** Lobby rules. Empty: the project's default lobby rules (Project Settings > CoopKit Sessions). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCKSessionsLobbyRulesData> Rules;

	UPROPERTY(ReplicatedUsing = OnRep_Phase)
	ECKSessionsLobbyPhase Phase = ECKSessionsLobbyPhase::Waiting;

	/** Server world time when the countdown ends. */
	UPROPERTY(Replicated)
	double CountdownEndSeconds = 0.0;

	bool bMissingRulesReported = false;
};
