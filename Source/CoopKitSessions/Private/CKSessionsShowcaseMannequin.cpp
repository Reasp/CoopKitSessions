// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.
// CoopKit shared file: the same mannequin pawn ships in every CoopKit plugin under the plugin's prefix.

#include "CKSessionsShowcaseMannequin.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "UObject/ConstructorHelpers.h"

namespace CKSessions::Showcase::Mannequin
{
	namespace
	{
		/** The pawn's sphere is 35 cm in radius and rests 2 cm over the stand: the feet are this far under its center. */
		constexpr float FeetBelowRoot = 37.0f;

		/** The tutorial mannequin looks along its +Y axis; the pawn looks along +X. */
		constexpr float MeshYaw = -90.0f;

		/** Length of the mannequin from heel to crown, and the height of its back over the floor when it lies, in centimeters. */
		constexpr float BodyLength = 192.0f;
		constexpr float LyingRest = 16.0f;

		/** Speed of the walk sample of the blend space, in cm/s: faster pawns play the walk faster. */
		constexpr float WalkSampleSpeed = 125.0f;
		constexpr float MaxPlayRate = 3.0f;

		/** Smoothing of the measured speed, in 1/s, and the largest step of one frame that still counts as a walk (a bigger one is a teleport). */
		constexpr float SpeedSmoothing = 10.0f;
		constexpr float MaxWalkStep = 300.0f;

		/** Speed over which the mannequin turns to where it goes, in cm/s, and how fast it turns, in degrees per second. */
		constexpr float TurnSpeed = 30.0f;
		constexpr float TurnRate = 540.0f;

		/** Share of the fall per second when it lies down or gets up. */
		constexpr float LieRate = 4.0f;

		/**
		 * The label of a standing mannequin stands on the floor in front of it: over the head it would sit on the stand's
		 * sign, which is right behind a standing bot. A lying body is low, so its label hovers over it instead (in front
		 * of a long body at a low angle it would cover the body). Heights are over the floor, in centimeters.
		 */
		constexpr float StandingLabelDistance = 60.0f;
		constexpr float StandingLabelLift = 4.0f;
		constexpr float LyingLabelLift = 55.0f;
	}
}

ACKSessionsShowcaseMannequin::ACKSessionsShowcaseMannequin()
{
	using namespace CKSessions::Showcase::Mannequin;

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshFinder(TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/TutorialTPP.TutorialTPP"));
	static ConstructorHelpers::FObjectFinder<UAnimationAsset> BlendFinder(TEXT("/Engine/Tutorial/SubEditors/TutorialAssets/Character/NewBlendSpace1D.NewBlendSpace1D"));
	// Optional: a plugin without the material keeps the mannequin's own material, quietly.
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> MaterialFinder(TEXT("/CoopKitSessions/Materials/M_CKSessionsShowcaseMannequin.M_CKSessionsShowcaseMannequin"), LOAD_Quiet | LOAD_NoWarn);
	WalkBlend = BlendFinder.Object;
	ColorMaterial = MaterialFinder.Get();

	PrimaryActorTick.bCanEverTick = true;

	// The default pawn's sphere is only collision here.
	if (UStaticMeshComponent* Sphere = GetMeshComponent())
	{
		Sphere->SetVisibility(false);
		Sphere->SetHiddenInGame(true);
	}

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mannequin"));
	Body->SetupAttachment(RootComponent);
	Body->SetSkeletalMesh(MeshFinder.Object);
	Body->SetRelativeLocationAndRotation(FVector(0.0, 0.0, -FeetBelowRoot), FRotator(0.0, MeshYaw, 0.0));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);
	Body->bOwnerNoSee = true;

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(RootComponent);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextBottom);
	Label->SetWorldSize(26.0f);
	Label->SetTextRenderColor(FColor::White);
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Label->SetCastShadow(false);
	Label->bOwnerNoSee = true;
}

void ACKSessionsShowcaseMannequin::BeginPlay()
{
	Super::BeginPlay();

	// Only the look depends on the frame: a dedicated server and a game without a renderer have nothing to show.
	bShown = GetNetMode() != NM_DedicatedServer && FApp::CanEverRender();
	Body->SetVisibility(bShown);
	Label->SetVisibility(bShown);
	SetActorTickEnabled(bShown);
	if (!bShown)
	{
		return;
	}

	// The blend space is driven by hand: idle at speed 0, walk at the speed the pawn moves.
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Body->SetAnimation(WalkBlend);
	Body->Play(true);
	if (ColorMaterial != nullptr)
	{
		BodyMaterial = UMaterialInstanceDynamic::Create(ColorMaterial, this);
		Body->SetMaterial(0, BodyMaterial);
	}
}

void ACKSessionsShowcaseMannequin::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	FCKSessionsShowcaseLook Look;
	GetLook(Look);
	ShowLook(Look);
	if (Look.Pose == ECKSessionsShowcasePose::Ragdoll)
	{
		EnterRagdoll();
	}
	if (!bRagdoll)
	{
		UpdateWalk(DeltaSeconds);
		UpdateLie(DeltaSeconds, Look.Pose == ECKSessionsShowcasePose::Lying);
	}
	PlaceLabel();
}

void ACKSessionsShowcaseMannequin::GetLook(FCKSessionsShowcaseLook& OutLook) const
{
	const APlayerState* Player = GetPlayerState();
	OutLook.Label = Player != nullptr ? Player->GetPlayerName() : FString();
}

void ACKSessionsShowcaseMannequin::ShowLook(const FCKSessionsShowcaseLook& Look)
{
	if (BodyMaterial != nullptr && !Look.Color.Equals(ShownColor))
	{
		ShownColor = Look.Color;
		BodyMaterial->SetVectorParameterValue(TEXT("Color"), Look.Color);
	}
	if (Look.Label != ShownLabel)
	{
		ShownLabel = Look.Label;
		Label->SetText(FText::FromString(Look.Label));
	}
}

void ACKSessionsShowcaseMannequin::UpdateWalk(const float DeltaSeconds)
{
	using namespace CKSessions::Showcase::Mannequin;

	// Bots are moved by the server's SetActorLocation, so the speed is the displacement of the replicated pawn.
	const FVector Location = GetActorLocation();
	float Measured = 0.0f;
	if (bHasLastLocation && DeltaSeconds > UE_KINDA_SMALL_NUMBER)
	{
		const FVector Step = (Location - LastLocation) * FVector(1.0, 1.0, 0.0);
		if (Step.Size() <= MaxWalkStep)
		{
			Measured = static_cast<float>(Step.Size() / DeltaSeconds);
			if (Measured > TurnSpeed)
			{
				MoveYaw = static_cast<float>(Step.Rotation().Yaw);
			}
		}
	}
	LastLocation = Location;
	bHasLastLocation = true;
	WalkSpeed = FMath::FInterpTo(WalkSpeed, Measured, DeltaSeconds, SpeedSmoothing);

	// Walk forward: the mannequin turns to where it goes and turns back to the pawn's heading when it stops.
	const float Wanted = WalkSpeed > TurnSpeed ? FMath::FindDeltaAngleDegrees(static_cast<float>(GetActorRotation().Yaw), MoveYaw) : 0.0f;
	TurnYaw = FMath::FixedTurn(TurnYaw, Wanted, TurnRate * DeltaSeconds);

	if (UAnimSingleNodeInstance* Node = Body->GetSingleNodeInstance())
	{
		Node->SetBlendSpacePosition(FVector(WalkSpeed, 0.0, 0.0));
		Node->SetPlayRate(FMath::Clamp(WalkSpeed / WalkSampleSpeed, 1.0f, MaxPlayRate));
	}
}

void ACKSessionsShowcaseMannequin::UpdateLie(const float DeltaSeconds, const bool bLying)
{
	using namespace CKSessions::Showcase::Mannequin;

	LieAlpha = FMath::FInterpConstantTo(LieAlpha, bLying ? 1.0f : 0.0f, DeltaSeconds, LieRate);
	const float Fall = FMath::InterpEaseInOut(0.0f, 1.0f, LieAlpha, 2.0f);

	// Falling on its back, across the pawn's heading so that a viewer in front sees it broadside: the mannequin turns a
	// quarter, pitches about its heels, then slides to center the body on the pawn and rests on the floor. Quaternions
	// apply right to left: the mesh fix first, the turn to where it walks last.
	const float Angle = 90.0f * Fall;
	const float Sine = FMath::Sin(FMath::DegreesToRadians(Angle));
	const FQuat Turn = FRotator(0.0, TurnYaw, 0.0).Quaternion();
	const FQuat Across = FRotator(0.0, Angle, 0.0).Quaternion();
	const FQuat Pitch = FRotator(Angle, 0.0, 0.0).Quaternion();
	const FQuat Fix = FRotator(0.0, MeshYaw, 0.0).Quaternion();
	const FVector Shift(0.5f * BodyLength * Sine, 0.0f, -FeetBelowRoot + LyingRest * Sine);
	Body->SetRelativeLocationAndRotation(Turn.RotateVector(Across.RotateVector(Shift)), Turn * Across * Pitch * Fix);
}

void ACKSessionsShowcaseMannequin::EnterRagdoll()
{
	if (bRagdoll)
	{
		return;
	}
	bRagdoll = true;
	LieAlpha = 1.0f;

	// The body leaves the pawn and falls on the physics asset of the mannequin; only the stands and the floor stop it.
	Body->Stop();
	Body->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionObjectType(ECC_PhysicsBody);
	Body->SetCollisionResponseToAllChannels(ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Body->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Body->SetAllBodiesSimulatePhysics(true);
	Body->SetSimulatePhysics(true);
	Body->WakeAllRigidBodies();
	Body->bBlendPhysics = true;
}

void ACKSessionsShowcaseMannequin::PlaceLabel()
{
	using namespace CKSessions::Showcase::Mannequin;

	const UWorld* World = GetWorld();
	const APlayerController* Viewer = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	if (Viewer == nullptr || Viewer->PlayerCameraManager == nullptr)
	{
		return;
	}
	const FVector Center = GetActorLocation();
	const FVector ToViewer = (Viewer->PlayerCameraManager->GetCameraLocation() - Center) * FVector(1.0, 1.0, 0.0);
	const float Fall = FMath::InterpEaseInOut(0.0f, 1.0f, LieAlpha, 2.0f);
	const FVector Spot = Center + ToViewer.GetSafeNormal() * FMath::Lerp(StandingLabelDistance, 0.0f, Fall) + FVector(0.0, 0.0, FMath::Lerp(StandingLabelLift, LyingLabelLift, Fall) - FeetBelowRoot);
	Label->SetWorldLocationAndRotation(Spot, FRotator(0.0, ToViewer.Rotation().Yaw, 0.0));
}
