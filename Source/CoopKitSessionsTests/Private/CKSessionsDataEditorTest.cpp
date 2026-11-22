// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsDataEditorToolkit.h"
#include "CKSessionsDataValidation.h"
#include "CKSessionsLobbyRulesData.h"
#include "CKSessionsProfileData.h"
#include "Editor.h"
#include "Factories/Factory.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CKSessions::Tests
{
	namespace
	{
		/** The factory the Content Browser would offer for the class: a UFactory that creates new assets of it. */
		UFactory* FindCreateNewFactory(const UClass* DataClass)
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (!It->IsChildOf(UFactory::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract))
				{
					continue;
				}
				UFactory* Factory = It->GetDefaultObject<UFactory>();
				if (Factory->CanCreateNew() && Factory->GetSupportedClass() == DataClass)
				{
					return Factory;
				}
			}
			return nullptr;
		}

		/** Changes a property the way the Details panel does: set the value, then announce the change. */
		template <typename InPropertyType, typename InValueType>
		void EditProperty(UObject& Data, const TCHAR* PropertyName, const InValueType& Value)
		{
			InPropertyType* Property = FindFProperty<InPropertyType>(Data.GetClass(), PropertyName);
			check(Property != nullptr);
			Data.PreEditChange(Property);
			Property->SetPropertyValue_InContainer(&Data, Value);
			FPropertyChangedEvent Event(Property);
			Data.PostEditChangeProperty(Event);
		}

		bool ContainsText(const TArray<FText>& Lines, const TCHAR* Fragment)
		{
			return Lines.ContainsByPredicate([Fragment](const FText& Line) { return Line.ToString().Contains(Fragment); });
		}

		/** Creates a new asset of the class through the factory the Content Browser uses. */
		UObject* CreateThroughFactory(FAutomationTestBase& Test, UClass* DataClass)
		{
			UFactory* Factory = FindCreateNewFactory(DataClass);
			if (!Test.TestNotNull(FString::Printf(TEXT("A factory creates new %s (Content Browser > Add)"), *DataClass->GetName()), Factory))
			{
				return nullptr;
			}
			const FString AssetName = FString::Printf(TEXT("%s_Test_%s"), *DataClass->GetName(), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
			UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/CoopKitTests/%s"), *AssetName));
			UObject* Data = Factory->FactoryCreateNew(DataClass, Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional, nullptr, GWarn);
			Test.TestTrue(FString::Printf(TEXT("Factory output is %s"), *DataClass->GetName()), Data != nullptr && Data->IsA(DataClass));
			return Data;
		}

		/**
		 * Opens the asset the way a double click does and checks the plugin's data editor: the Summary tab shows the
		 * expected fragment, the Validation tab starts valid and turns invalid after Break, naming the broken field.
		 */
		void CheckDataEditor(FAutomationTestBase& Test, UObject& Data, const TCHAR* SummaryFragment, const TFunction<void()>& Break, const TCHAR* BrokenField)
		{
			UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			Test.TestTrue(TEXT("Editor opens"), AssetEditors->OpenEditorForAsset(&Data));
			IAssetEditorInstance* Instance = AssetEditors->FindEditorForAsset(&Data, false);
			if (Test.TestNotNull(TEXT("Editor instance for the asset"), Instance)
				&& Test.TestEqual(TEXT("Opened editor is the plugin's data editor"), Instance->GetEditorName(), FCKSessionsDataEditorToolkit::ToolkitName))
			{
				const FCKSessionsDataEditorToolkit* Editor = static_cast<FCKSessionsDataEditorToolkit*>(Instance);
				Test.TestTrue(FString::Printf(TEXT("Summary tab shows \"%s\""), SummaryFragment), Editor->GetSummaryText().ToString().Contains(SummaryFragment));
				Test.TestEqual(TEXT("Validation tab starts valid"), Editor->GetValidationReport().Result, EDataValidationResult::Valid);

				// The Validation tab follows edits made while the editor is open.
				Break();
				Test.TestEqual(TEXT("Validation tab turns invalid after an edit"), Editor->GetValidationReport().Result, EDataValidationResult::Invalid);
				Test.TestTrue(FString::Printf(TEXT("Validation tab names %s"), BrokenField), ContainsText(Editor->GetValidationReport().Errors, BrokenField));
				Test.AddInfo(FString::Printf(TEXT("Validation tab: %s"), *Editor->GetValidationReport().ToText().ToString()));
			}

			// Without rendering (-nullrhi) a closing editor window asks the generic platform window for its restored size to save
			// the tab layout, and FGenericWindow::GetRestoredDimensions is a fatal error (SDockingArea::GatherPersistentLayout,
			// UE 5.4 and 5.8). Headless runs therefore leave the window open until the editor exits; a rendering editor closes it.
			if (FApp::CanEverRender())
			{
				AssetEditors->CloseAllEditorsForAsset(&Data);
				Data.ClearFlags(RF_Public | RF_Standalone);
				Data.MarkAsGarbage();
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsProfileEditorTest, "CoopKit.Sessions.Data.ProfileEditor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsProfileEditorTest::RunTest(const FString& Parameters)
{
	using namespace CKSessions::Tests;

	UObject* Data = CreateThroughFactory(*this, UCKSessionsProfileData::StaticClass());
	if (Data == nullptr)
	{
		return false;
	}

	// A new profile is usable as is; the checks catch what breaks sessions.
	TestEqual(TEXT("New profile is valid"), CKSessions::Editor::ValidateData(*Data).Result, EDataValidationResult::Valid);
	EditProperty<FNameProperty>(*Data, TEXT("SessionType"), NAME_None);
	const CKSessions::Editor::FValidationReport NoType = CKSessions::Editor::ValidateData(*Data);
	TestEqual(TEXT("Profile without a session type is invalid"), NoType.Result, EDataValidationResult::Invalid);
	TestTrue(TEXT("Error names Session Type"), ContainsText(NoType.Errors, TEXT("Session Type")));
	EditProperty<FNameProperty>(*Data, TEXT("SessionType"), FName(TEXT("Campaign")));

	CheckDataEditor(*this, *Data, TEXT("Session type: Campaign"),
		[Data]() { EditProperty<FIntProperty>(*Data, TEXT("MaxPlayers"), 1); },
		TEXT("Max Players"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsLobbyRulesEditorTest, "CoopKit.Sessions.Data.LobbyRulesEditor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsLobbyRulesEditorTest::RunTest(const FString& Parameters)
{
	using namespace CKSessions::Tests;

	UObject* Data = CreateThroughFactory(*this, UCKSessionsLobbyRulesData::StaticClass());
	if (Data == nullptr)
	{
		return false;
	}

	// New rules have no match map: the lobby would have nowhere to travel.
	const CKSessions::Editor::FValidationReport Fresh = CKSessions::Editor::ValidateData(*Data);
	TestEqual(TEXT("New lobby rules are invalid"), Fresh.Result, EDataValidationResult::Invalid);
	TestTrue(TEXT("Error names Match Map"), ContainsText(Fresh.Errors, TEXT("Match Map")));
	EditProperty<FSoftObjectProperty>(*Data, TEXT("MatchMap"), FSoftObjectPtr(FSoftObjectPath(TEXT("/Game/CoopKitSample/Maps/L_ProvingGround.L_ProvingGround"))));
	TestEqual(TEXT("Rules with a match map are valid"), CKSessions::Editor::ValidateData(*Data).Result, EDataValidationResult::Valid);

	CheckDataEditor(*this, *Data, TEXT("L_ProvingGround"),
		[Data]() { EditProperty<FFloatProperty>(*Data, TEXT("CountdownSeconds"), -1.0f); },
		TEXT("Countdown Seconds"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
