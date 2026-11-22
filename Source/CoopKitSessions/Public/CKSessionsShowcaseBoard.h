// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CKSessionsShowcaseScenario.h"
#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "CKSessionsShowcaseBoard.generated.h"

class APawn;
class APlayerController;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

/** What a stand of the overview map shows. */
UENUM(BlueprintType)
enum class ECKSessionsShowcaseMode : uint8
{
	/** A board with the session state of this game, live, and the commands that host, find and join. */
	Session,
	/** Three bots toggle ready in the map's real lobby: the countdown starts, then one of them cancels it. */
	Lobby,
	/** Two bots with the host's records: one leaves the game and comes back, the host recognizes him. */
	Roster,
	/** A board with the Blueprint nodes and the C++ classes to start from. */
	Api
};

/**
 * Scenario of the CoopKit Sessions overview map. One process cannot host, search and join a session at once, so the
 * stands show what does run in one world: the live state of the sessions subsystem, the real lobby component with ready
 * players (bots), and the host's records of players who leave and come back. The lobby never travels: the bots cancel
 * the countdown before it ends. Hosting, searching, joining and joining a match in progress need several game
 * instances (see the plugin's automation tests); the board of the first stand and the last stand say so.
 */
UCLASS(Blueprintable)
class COOPKITSESSIONS_API ACKSessionsShowcaseBoard : public ACKSessionsShowcaseScenario
{
	GENERATED_BODY()

public:
	ACKSessionsShowcaseBoard();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	ECKSessionsShowcaseMode GetMode() const
	{
		return Mode;
	}

	/** The text of the board, as drawn now. */
	FString GetBoardText() const
	{
		return BoardText;
	}

protected:
	virtual void RunStep_Implementation(int32 Step) override;

	/** What this stand shows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Sessions")
	ECKSessionsShowcaseMode Mode = ECKSessionsShowcaseMode::Session;

private:
	void BuildBoard();
	UStaticMeshComponent* AddBlock(const FName Name, const FLinearColor& Color);
	UTextRenderComponent* AddText(const FName Name, const FVector& Location, float Size, const FColor& Color);
	void UpdateBoard(double Now);
	FString MakeBoardText() const;

	/** Bots of the Lobby and Roster stands: names, and who stands where. */
	TArray<FString> GetBotNames() const;
	APlayerController* FindBot(int32 Index) const;
	APlayerController* AddBotNamed(int32 Index);
	FVector GetBotSpot(int32 Index) const;
	void StepLobby(int32 Step);
	void StepRoster(int32 Step);
	void SetBotReady(int32 Index, bool bReady) const;
	void ResetGuestsReady() const;

	/** Bots by index in the name list; the weak pointer empties when a bot leaves. */
	TArray<TWeakObjectPtr<APlayerController>> Roster;

	/** Pawns already put at their spots. */
	TSet<TWeakObjectPtr<APawn>> PlacedBots;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> ShapeMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UTextRenderComponent> BoardBody;

	FString BoardText;

	/** The description of the stand as the map sets it; the live line of a bot stand goes under it. */
	FText BaseDescription;
	double NextBoardUpdate = 0.0;
};
