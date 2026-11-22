// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsProfileDataFactory.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "CKSessionsProfileData.h"

#define LOCTEXT_NAMESPACE "CKSessionsProfileDataFactory"

namespace
{
	/** Copies every property an asset shows in the Details panel from Source to Target (the data class keeps its fields private). */
	void CopyProfileWizardValues(const UObject& Source, UObject& Target)
	{
		check(Source.GetClass() == Target.GetClass());
		for (TFieldIterator<FProperty> It(Target.GetClass()); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Edit))
			{
				It->CopyCompleteValue_InContainer(&Target, &Source);
			}
		}
	}

	/** Writes the session type the way the Details panel does (no transaction: the profile is new). */
	void SetProfileWizardSessionType(UObject& Profile, const FName Type)
	{
		FNameProperty* Property = FindFProperty<FNameProperty>(Profile.GetClass(), TEXT("SessionType"));
		check(Property != nullptr);
		Property->SetPropertyValue_InContainer(&Profile, Type);
	}
}

UCKSessionsProfileDataFactory::UCKSessionsProfileDataFactory()
{
	SupportedClass = UCKSessionsProfileData::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

bool UCKSessionsProfileDataFactory::ConfigureProperties()
{
	if (FApp::IsUnattended() || !FSlateApplication::IsInitialized())
	{
		return true;
	}
	// Shared, not a local captured by reference: the button's handler lives in the widget tree.
	const TSharedRef<bool> bAccepted = MakeShared<bool>(false);
	GEditor->EditorAddModalWindow(MakeWizardWindow(bAccepted));
	return *bAccepted;
}

TSharedRef<SWindow> UCKSessionsProfileDataFactory::MakeWizardWindow(const TSharedRef<bool>& bOutAccepted)
{
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs Args;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	Args.bAllowSearch = false;
	const TSharedRef<IDetailsView> Details = PropertyEditor.CreateDetailView(Args);
	Details->SetObject(this);

	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("Title", "New Sessions Profile"))
		.ClientSize(FVector2D(560.0, 340.0))
		.SupportsMinimize(false)
		.SupportsMaximize(false);
	TWeakPtr<SWindow> WeakWindow = Window;
	Window->SetContent(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(LOCTEXT("Intro", "The wizard creates the sessions profile in its own folder, starting from the plugin's defaults or from another profile. "
				"The session type keeps the game's sessions apart from other games and versions. Players, scope, reconnection and search are edited in the card afterwards."))
		]
		+ SVerticalBox::Slot().FillHeight(1.0f).Padding(4.0f)
		[
			Details
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Create", "Create"))
				.OnClicked_Lambda([bOutAccepted, WeakWindow]()
				{
					*bOutAccepted = true;
					if (const TSharedPtr<SWindow> Pinned = WeakWindow.Pin())
					{
						Pinned->RequestDestroyWindow();
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Cancel", "Cancel"))
				.OnClicked_Lambda([WeakWindow]()
				{
					if (const TSharedPtr<SWindow> Pinned = WeakWindow.Pin())
					{
						Pinned->RequestDestroyWindow();
					}
					return FReply::Handled();
				})
			]
		]);
	return Window;
}

UObject* UCKSessionsProfileDataFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, const FName InName, const EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UCKSessionsProfileData* Data = NewObject<UCKSessionsProfileData>(InParent, InClass, InName, Flags | RF_Transactional);
	if (const UCKSessionsProfileData* Source = StartFrom.LoadSynchronous())
	{
		CopyProfileWizardValues(*Source, *Data);
	}
	if (!SessionType.IsNone())
	{
		SetProfileWizardSessionType(*Data, SessionType);
	}
	return Data;
}

#undef LOCTEXT_NAMESPACE
