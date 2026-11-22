// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "UObject/SoftObjectPtr.h"
#include "CKSessionsProfileDataFactory.generated.h"

class SWindow;
class UCKSessionsProfileData;

/**
 * The New Sessions Profile wizard (Content Browser > Add > CoopKit > Sessions Profile, New Sessions Profile in the CoopKit Hub). The
 * options below are asked in a window before the asset is created; then the factory creates the profile
 * DA_SessionsProfile_<Name> in its own folder. A profile has no parts: the kit is the profile alone.
 */
UCLASS(HideCategories = Object)
class COOPKITSESSIONSEDITOR_API UCKSessionsProfileDataFactory final : public UFactory
{
	GENERATED_BODY()

public:
	UCKSessionsProfileDataFactory();

	/** Shows the wizard window; false when the user cancels. Without a window (unattended runs) the options stay as set. */
	virtual bool ConfigureProperties() override;

	/**
	 * The wizard window over this factory's options; bOutAccepted turns true when the user presses Create. ConfigureProperties
	 * shows it modally; tests and screenshots may show it as a normal window.
	 */
	TSharedRef<SWindow> MakeWizardWindow(const TSharedRef<bool>& bOutAccepted);

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;

	/** The profile (type, players, scope, reconnection, search) to start from. Empty: the plugin's defaults. */
	UPROPERTY(EditAnywhere, Category = "New")
	TSoftObjectPtr<UCKSessionsProfileData> StartFrom;

	/**
	 * Session Type of the new profile: a search only returns sessions of the same type, so the game's sessions never meet
	 * other games or versions. Empty: the one of the profile to start from, else the plugin's default.
	 */
	UPROPERTY(EditAnywhere, Category = "New")
	FName SessionType;
};
