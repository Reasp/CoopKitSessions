// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.
// CoopKit shared file: the same scenario base class ships in every CoopKit plugin under the plugin's prefix.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CKSessionsShowcaseScenario.generated.h"

class ACKSessionsShowcaseStand;
class APawn;
class APlayerController;

/**
 * Base of a scenario that plays one feature of the plugin on a stand of the overview map, in a loop, on the server.
 * Every Step Seconds the server runs the next step (0 .. Step Count - 1, then again from 0); subclasses do the work in
 * Run Step (C++ or Blueprint). A scenario may add bot players: local players without a view of their own, with a
 * controller and a pawn like any player, so the plugin treats them as real players. Bots need a game viewport (play in
 * the editor, standalone or listen server); on a dedicated server the scenario runs without them.
 */
UCLASS(Abstract, Blueprintable)
class COOPKITSESSIONS_API ACKSessionsShowcaseScenario : public AActor
{
	GENERATED_BODY()

public:
	ACKSessionsShowcaseScenario();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Stand this scenario plays on (for its texts); may be null. */
	ACKSessionsShowcaseStand* GetStand() const
	{
		return Stand;
	}

	/** Server: bots this scenario created, in creation order. */
	const TArray<TObjectPtr<APlayerController>>& GetBots() const
	{
		return Bots;
	}

protected:
	/** Server: one step of the scenario; Step counts from 0 and starts again after Step Count. */
	UFUNCTION(BlueprintNativeEvent, BlueprintAuthorityOnly, Category = "CoopKit|Showcase")
	void RunStep(int32 Step);
	virtual void RunStep_Implementation(int32 Step);

	/**
	 * Server: adds a bot player named Name and puts its pawn at Location facing Rotation.
	 * @param PawnClass Pawn for the bot instead of the game mode's default pawn; none keeps the default.
	 * @return The bot's controller; null when the world cannot have local players (dedicated server, no viewport).
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "CoopKit|Showcase")
	APlayerController* CreateBot(const FString& Name, const FVector& Location, const FRotator& Rotation, TSubclassOf<APawn> PawnClass = nullptr);

	/** Server: puts the bot's pawn at Location, its view facing Rotation (the pawn may spawn after the bot). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "CoopKit|Showcase")
	static void PlaceBot(APlayerController* Bot, const FVector& Location, const FRotator& Rotation);

	/** Server: turns the bot's view to look at Target from its pawn. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "CoopKit|Showcase")
	static void AimBot(APlayerController* Bot, const FVector& Target);

	/** Stand this scenario plays on. */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "CoopKit|Showcase")
	TObjectPtr<ACKSessionsShowcaseStand> Stand;

	/** Seconds between steps. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Showcase", meta = (ClampMin = "0.1"))
	float StepSeconds = 3.0f;

	/** Steps in one loop of the scenario. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Showcase", meta = (ClampMin = "1"))
	int32 StepCount = 4;

	/** Seconds after the match starts before the first step. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Showcase", meta = (ClampMin = "0.0"))
	float FirstStepDelaySeconds = 1.5f;

private:
	void AdvanceStep();

	FTimerHandle StepTimer;
	int32 NextStep = 0;
	bool bDisabledSplitscreen = false;
	/** Split-screen player limit of the viewport before this scenario raised it for its bots; INDEX_NONE when untouched. */
	int32 RaisedPlayerLimitFrom = INDEX_NONE;

	UPROPERTY(Transient)
	TArray<TObjectPtr<APlayerController>> Bots;
};
