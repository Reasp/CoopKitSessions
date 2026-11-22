// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.
// CoopKit shared file: the same stand class ships in every CoopKit plugin under the plugin's prefix.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CKSessionsShowcaseStand.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * A stand of the plugin's overview map: a floor platform with a colored front edge and a sign with a number, a title
 * and a short description. Each stand shows one feature; the actors of the feature stand on the platform. Edit the
 * texts and the color in the Details panel. A number of 0 hides the number (use it for the welcome sign).
 * Built only from engine content (basic shapes, engine materials and font).
 */
UCLASS(Blueprintable)
class COOPKITSESSIONS_API ACKSessionsShowcaseStand : public AActor
{
	GENERATED_BODY()

public:
	ACKSessionsShowcaseStand();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;

	/** Number shown on the sign; 0 hides it. */
	int32 GetNumber() const
	{
		return Number;
	}

	/** Title shown on the sign. */
	FText GetTitle() const
	{
		return Title;
	}

	/** Description shown under the title. */
	FText GetDescription() const
	{
		return Description;
	}

	/** Updates the texts at runtime, for example to show the state of the stand's scenario. */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Showcase")
	void SetTexts(const FText& NewTitle, const FText& NewDescription);

	/** World point at the center of the platform's top, where the feature's actors stand. */
	UFUNCTION(BlueprintPure, Category = "CoopKit|Showcase")
	FVector GetPlatformCenter() const;

protected:
	/** Number shown on the sign; 0 hides it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Showcase", meta = (ClampMin = "0"))
	int32 Number = 1;

	/** Title shown on the sign. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Showcase")
	FText Title;

	/** Description shown under the title; a new line in the text starts a new line on the sign. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Showcase", meta = (MultiLine = "true"))
	FText Description;

	/** Color of the front edge and the number. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Showcase")
	FLinearColor AccentColor = FLinearColor(0.1f, 0.55f, 1.0f);

	/** Size of the platform in centimeters: depth (X) and width (Y). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Showcase", meta = (ClampMin = "100"))
	FVector2D PlatformSize = FVector2D(800.0, 800.0);

	/** Height of the title letters in centimeters; the number is twice as tall, the description half. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CoopKit|Showcase", meta = (ClampMin = "5"))
	float TextSize = 40.0f;

private:
	void ApplyLayout();
	void ApplyTexts();
	UMaterialInstanceDynamic* MakeColorMaterial(UStaticMeshComponent& Mesh, const FLinearColor& Color);

	/** Engine material the colors are made from (its Color parameter); referenced here so cooking keeps it. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ColorMaterial;

	UPROPERTY(VisibleAnywhere, Category = "CoopKit|Showcase")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "CoopKit|Showcase")
	TObjectPtr<UStaticMeshComponent> Platform;

	UPROPERTY(VisibleAnywhere, Category = "CoopKit|Showcase")
	TObjectPtr<UStaticMeshComponent> Edge;

	UPROPERTY(VisibleAnywhere, Category = "CoopKit|Showcase")
	TObjectPtr<UStaticMeshComponent> Sign;

	UPROPERTY(VisibleAnywhere, Category = "CoopKit|Showcase")
	TObjectPtr<UStaticMeshComponent> LeftPost;

	UPROPERTY(VisibleAnywhere, Category = "CoopKit|Showcase")
	TObjectPtr<UStaticMeshComponent> RightPost;

	UPROPERTY(VisibleAnywhere, Category = "CoopKit|Showcase")
	TObjectPtr<UTextRenderComponent> NumberText;

	UPROPERTY(VisibleAnywhere, Category = "CoopKit|Showcase")
	TObjectPtr<UTextRenderComponent> TitleText;

	UPROPERTY(VisibleAnywhere, Category = "CoopKit|Showcase")
	TObjectPtr<UTextRenderComponent> DescriptionText;
};
