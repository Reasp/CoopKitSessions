// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.
// CoopKit shared file: the same stand class ships in every CoopKit plugin under the plugin's prefix.

#include "CKSessionsShowcaseStand.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace CKSessionsShowcaseStand
{
	// Layout in centimeters. The stand faces +X: visitors look at the sign from the platform's front edge.
	constexpr double PlatformHeight = 10.0;
	constexpr double EdgeDepth = 20.0;
	constexpr double SignThickness = 10.0;
	constexpr double SignBottom = 60.0;
	constexpr double SignHeight = 240.0;
	constexpr double SignWidthShare = 0.9;
	constexpr double TextMargin = 30.0;
	constexpr double PostSize = 12.0;
	constexpr double PostInset = 40.0;
	constexpr double DescriptionShare = 0.65;

	const FLinearColor PlatformColor(0.035f, 0.037f, 0.042f);
	const FLinearColor SignColor(0.012f, 0.013f, 0.016f);
	const FColor TitleColor(245, 245, 245);
	const FColor DescriptionColor(190, 196, 205);

	/** Text render components break lines on <br>, not on new line characters. */
	FText ToSignText(const FText& Text)
	{
		FString String = Text.ToString();
		String.ReplaceInline(TEXT("\r\n"), TEXT("<br>"));
		String.ReplaceInline(TEXT("\n"), TEXT("<br>"));
		return FText::FromString(String);
	}

	UStaticMeshComponent* MakeBlock(AActor& Owner, USceneComponent& Parent, UStaticMesh* Mesh, const FName Name)
	{
		UStaticMeshComponent* Block = Owner.CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Block->SetupAttachment(&Parent);
		Block->SetStaticMesh(Mesh);
		Block->SetMobility(EComponentMobility::Movable);
		return Block;
	}

	UTextRenderComponent* MakeText(AActor& Owner, USceneComponent& Parent, const FName Name)
	{
		UTextRenderComponent* Text = Owner.CreateDefaultSubobject<UTextRenderComponent>(Name);
		Text->SetupAttachment(&Parent);
		Text->SetHorizontalAlignment(EHTA_Left);
		Text->SetVerticalAlignment(EVRTA_TextTop);
		Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return Text;
	}
}

ACKSessionsShowcaseStand::ACKSessionsShowcaseStand()
{
	PrimaryActorTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	ColorMaterial = MaterialFinder.Object;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Platform = CKSessionsShowcaseStand::MakeBlock(*this, *Root, CubeFinder.Object, TEXT("Platform"));
	Edge = CKSessionsShowcaseStand::MakeBlock(*this, *Root, CubeFinder.Object, TEXT("Edge"));
	Sign = CKSessionsShowcaseStand::MakeBlock(*this, *Root, CubeFinder.Object, TEXT("Sign"));
	LeftPost = CKSessionsShowcaseStand::MakeBlock(*this, *Root, CubeFinder.Object, TEXT("LeftPost"));
	RightPost = CKSessionsShowcaseStand::MakeBlock(*this, *Root, CubeFinder.Object, TEXT("RightPost"));

	NumberText = CKSessionsShowcaseStand::MakeText(*this, *Root, TEXT("NumberText"));
	TitleText = CKSessionsShowcaseStand::MakeText(*this, *Root, TEXT("TitleText"));
	DescriptionText = CKSessionsShowcaseStand::MakeText(*this, *Root, TEXT("DescriptionText"));

	Title = NSLOCTEXT("CoopKitSessions", "ShowcaseStandTitle", "Feature");
}

void ACKSessionsShowcaseStand::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyLayout();
	ApplyTexts();
}

void ACKSessionsShowcaseStand::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Placed stands load with the components saved by the editor; build them again from the properties so a stand
	// edited outside the Details panel (scripts, a subclass's defaults) still shows its own texts and size.
	ApplyLayout();
	ApplyTexts();
}

void ACKSessionsShowcaseStand::SetTexts(const FText& NewTitle, const FText& NewDescription)
{
	Title = NewTitle;
	Description = NewDescription;
	ApplyTexts();
}

FVector ACKSessionsShowcaseStand::GetPlatformCenter() const
{
	return GetActorTransform().TransformPosition(FVector(0.0, 0.0, CKSessionsShowcaseStand::PlatformHeight));
}

void ACKSessionsShowcaseStand::ApplyLayout()
{
	using namespace CKSessionsShowcaseStand;
	const double Depth = PlatformSize.X;
	const double Width = PlatformSize.Y;
	const double SignWidth = Width * SignWidthShare;
	const double SignFront = -Depth * 0.5 + SignThickness;

	// The engine cube is 100 cm on each side and centered on its origin.
	Platform->SetRelativeLocation(FVector(0.0, 0.0, PlatformHeight * 0.5));
	Platform->SetRelativeScale3D(FVector(Depth / 100.0, Width / 100.0, PlatformHeight / 100.0));
	Edge->SetRelativeLocation(FVector(Depth * 0.5 + EdgeDepth * 0.5, 0.0, PlatformHeight * 0.6));
	Edge->SetRelativeScale3D(FVector(EdgeDepth / 100.0, Width / 100.0, PlatformHeight * 1.2 / 100.0));
	Sign->SetRelativeLocation(FVector(SignFront - SignThickness * 0.5, 0.0, SignBottom + SignHeight * 0.5));
	Sign->SetRelativeScale3D(FVector(SignThickness / 100.0, SignWidth / 100.0, SignHeight / 100.0));

	// Two posts carry the sign from the platform up to its bottom edge.
	const double PostHeight = SignBottom - PlatformHeight + SignHeight * 0.5;
	for (UStaticMeshComponent* Post : { LeftPost.Get(), RightPost.Get() })
	{
		const double Side = Post == LeftPost ? 1.0 : -1.0;
		Post->SetRelativeLocation(FVector(SignFront - SignThickness * 1.5, Side * (SignWidth * 0.5 - PostInset), PlatformHeight + PostHeight * 0.5));
		Post->SetRelativeScale3D(FVector(PostSize / 100.0, PostSize / 100.0, PostHeight / 100.0));
		MakeColorMaterial(*Post, SignColor);
	}

	MakeColorMaterial(*Platform, PlatformColor);
	MakeColorMaterial(*Sign, SignColor);
	MakeColorMaterial(*Edge, AccentColor);

	// Texts sit just in front of the sign and read from +X; the left edge of the text is the sign's +Y side.
	const double TextX = SignFront + 1.0;
	const double Left = SignWidth * 0.5 - TextMargin;
	const double Top = SignBottom + SignHeight - TextMargin;
	const double NumberSize = TextSize * 2.0;
	const double TitleLeft = Number > 0 ? Left - NumberSize * 1.1 : Left;

	NumberText->SetRelativeLocationAndRotation(FVector(TextX, Left, Top), FRotator::ZeroRotator);
	NumberText->SetWorldSize(NumberSize);
	NumberText->SetVisibility(Number > 0);

	TitleText->SetRelativeLocationAndRotation(FVector(TextX, TitleLeft, Top - TextSize * 0.25), FRotator::ZeroRotator);
	TitleText->SetWorldSize(TextSize);

	DescriptionText->SetRelativeLocationAndRotation(FVector(TextX, TitleLeft, Top - TextSize * 1.6), FRotator::ZeroRotator);
	DescriptionText->SetWorldSize(TextSize * DescriptionShare);
}

void ACKSessionsShowcaseStand::ApplyTexts()
{
	using namespace CKSessionsShowcaseStand;
	NumberText->SetText(FText::AsNumber(Number));
	NumberText->SetTextRenderColor(AccentColor.ToFColor(true));
	TitleText->SetText(ToSignText(Title));
	TitleText->SetTextRenderColor(TitleColor);
	DescriptionText->SetText(ToSignText(Description));
	DescriptionText->SetTextRenderColor(DescriptionColor);
}

UMaterialInstanceDynamic* ACKSessionsShowcaseStand::MakeColorMaterial(UStaticMeshComponent& Mesh, const FLinearColor& Color)
{
	if (!ColorMaterial)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(ColorMaterial, this);
	Material->SetVectorParameterValue(TEXT("Color"), Color);
	Mesh.SetMaterial(0, Material);
	return Material;
}
