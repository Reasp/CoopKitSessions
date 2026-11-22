// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.
// CoopKit shared file: the same mannequin pawn ships in every CoopKit plugin under the plugin's prefix.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/DefaultPawn.h"
#include "CKSessionsShowcaseMannequin.generated.h"

class UAnimationAsset;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMeshComponent;
class UTextRenderComponent;

/** Pose of a showcase mannequin. */
enum class ECKSessionsShowcasePose : uint8
{
	/** On its feet: idle, or walking at the speed the pawn actually moves. */
	Standing,
	/** Lying on its back, in the idle pose. */
	Lying,
	/** Limp body on the physics asset. One way: the pawn is replaced, never revived. */
	Ragdoll
};

/** What a showcase mannequin shows right now. */
struct FCKSessionsShowcaseLook
{
	/** Text near the mannequin; empty hides it. */
	FString Label;

	/** Tone of the body. */
	FLinearColor Color = FLinearColor(0.62f, 0.64f, 0.68f);

	ECKSessionsShowcasePose Pose = ECKSessionsShowcasePose::Standing;
};

/**
 * Pawn of a bot (or of the viewer) on an overview map: the engine's tutorial mannequin that stands, walks, lies and
 * falls limp. It is the engine's default pawn, so a human on the map still flies; the pawn's sphere is hidden and the
 * mannequin stands 37 cm under its center. Bots move by SetActorLocation: every machine takes the speed from the
 * displacement of the replicated pawn and walks the mannequin at that speed, so the server needs no animation.
 *
 * A subclass overrides GetLook and says what to show (label, tone, pose) from its own replicated state. Built only
 * from engine content; the tone comes from the plugin's own material, and without it the mannequin keeps its gray.
 * Machines without a renderer (dedicated server, no-RHI tests) show nothing and do not tick.
 */
UCLASS(NotPlaceable, NotBlueprintable)
class COOPKITSESSIONS_API ACKSessionsShowcaseMannequin : public ADefaultPawn
{
	GENERATED_BODY()

public:
	ACKSessionsShowcaseMannequin();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** The mannequin's skeletal mesh. */
	USkeletalMeshComponent* GetBodyMesh() const
	{
		return Body;
	}

	/** Whether this machine draws the mannequin (false on a dedicated server and without a renderer). */
	bool IsShown() const
	{
		return bShown;
	}

protected:
	/** What to show now; called every frame on machines that draw. The base shows the player's name, standing. */
	virtual void GetLook(FCKSessionsShowcaseLook& OutLook) const;

private:
	void ShowLook(const FCKSessionsShowcaseLook& Look);
	void UpdateWalk(float DeltaSeconds);
	void UpdateLie(float DeltaSeconds, bool bLying);
	void EnterRagdoll();
	void PlaceLabel();

	/** The mannequin. */
	UPROPERTY(VisibleAnywhere, Category = "CoopKit|Showcase")
	TObjectPtr<USkeletalMeshComponent> Body;

	/** Name and state, facing the viewer: on the floor in front of a standing pawn, over a lying one. */
	UPROPERTY(VisibleAnywhere, Category = "CoopKit|Showcase")
	TObjectPtr<UTextRenderComponent> Label;

	/** Idle-to-walk blend space of the tutorial mannequin (its axis is the speed in cm/s). */
	UPROPERTY()
	TObjectPtr<UAnimationAsset> WalkBlend;

	/** Plugin material with a Color parameter that can be drawn on a skeletal mesh; null when the plugin does not ship it. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ColorMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	bool bShown = false;
	bool bRagdoll = false;

	/** Position on the ground plane at the last frame, and the smoothed speed in cm/s. */
	FVector LastLocation = FVector::ZeroVector;
	bool bHasLastLocation = false;
	float WalkSpeed = 0.0f;

	/** World yaw of the last step that was a walk, and the yaw of the mannequin relative to the pawn: it turns where it walks. */
	float MoveYaw = 0.0f;
	float TurnYaw = 0.0f;

	/** 0 standing .. 1 lying on its back. */
	float LieAlpha = 0.0f;

	FString ShownLabel;
	FLinearColor ShownColor = FLinearColor::Transparent;
};
