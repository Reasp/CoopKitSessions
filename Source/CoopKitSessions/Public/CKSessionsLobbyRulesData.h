// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CKSessionsLobbyRulesData.generated.h"

class FDataValidationContext;
class UWorld;

/**
 * Lobby rules: when the lobby starts the match and where it travels.
 * The lobby waits for players, counts down once enough players are ready, then travels every player to the match map
 * (seamlessly when the game mode uses seamless travel) and marks the session as in progress.
 * Create one in Content Browser > Add > CoopKit > Sessions Lobby Rules; it opens in the CoopKit Sessions data editor.
 */
UCLASS(BlueprintType)
class COOPKITSESSIONS_API UCKSessionsLobbyRulesData final : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	int32 GetMinPlayersToStart() const
	{
		return MinPlayersToStart;
	}

	bool RequiresAllReady() const
	{
		return bRequireAllReady;
	}

	float GetCountdownSeconds() const
	{
		return CountdownSeconds;
	}

	const TSoftObjectPtr<UWorld>& GetMatchMap() const
	{
		return MatchMap;
	}

	const FString& GetTravelOptions() const
	{
		return TravelOptions;
	}

	bool CanHostForceStart() const
	{
		return bHostCanForceStart;
	}

	bool AddsPlayerComponents() const
	{
		return bAddPlayerComponents;
	}

#if WITH_EDITOR
	/** Reports rules the lobby cannot follow: no match map, fewer than one player, a negative countdown, malformed travel options. */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

private:
	/** Players needed before the countdown may start, the host included. At least 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions", meta = (AllowPrivateAccess = "true", ClampMin = "1", ClampMax = "64"))
	int32 MinPlayersToStart = 2;

	/** Every player must be ready before the countdown starts. Off: the countdown starts once Min Players To Start players are ready. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions", meta = (AllowPrivateAccess = "true"))
	bool bRequireAllReady = true;

	/** Seconds between "everyone is ready" and the travel to the match map. A player who stops being ready cancels it. 0 travels at once. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions", meta = (AllowPrivateAccess = "true", ClampMin = "0", Units = "s"))
	float CountdownSeconds = 5.0f;

	/** Map the lobby travels to when the match starts. Required. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions", meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<UWorld> MatchMap;

	/** Options appended to the travel URL, e.g. ?game=/Game/MyGameMode.MyGameMode_C. Each option starts with '?'. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions", meta = (AllowPrivateAccess = "true"))
	FString TravelOptions;

	/** The host may start the match before everyone is ready. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions", meta = (AllowPrivateAccess = "true"))
	bool bHostCanForceStart = true;

	/** The lobby adds the ready component to player states that do not have it. Off: add the component to your player state yourself. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions", meta = (AllowPrivateAccess = "true"))
	bool bAddPlayerComponents = true;
};
