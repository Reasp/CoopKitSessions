// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"
#include "SCKSessionsCard.h"
#include "SCKSessionsHub.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "CKSessionsDataEditorToolkit.h"
#include "CKSessionsDataValidation.h"
#include "CKSessionsHub.h"
#include "CKSessionsProfileData.h"
#include "CKSessionsProfileDataFactory.h"
#include "CKSessionsTestWorkspace.h"
#include "CKSessionsWorkspace.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CKSessions::Tests
{
	namespace
	{
		/** Asset names in a folder (unsaved assets included). */
		TArray<FString> SessionsWorkspaceTestAssetNamesIn(const FString& Folder)
		{
			TArray<FAssetData> Assets;
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByPath(FName(*Folder), Assets, true);
			TArray<FString> Names;
			for (const FAssetData& Asset : Assets)
			{
				if (!Asset.IsRedirector())
				{
					Names.Add(Asset.AssetName.ToString());
				}
			}
			Names.Sort();
			return Names;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsWorkspaceWizardTest, "CoopKit.Sessions.Workspace.Wizard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsWorkspaceWizardTest::RunTest(const FString& Parameters)
{
	using namespace CKSessions::Tests;

	const FScopedTestWorkspace Workspace;

	// Default options: the plugin's default profile, alone in its folder.
	const FString Name = MakeTestName(TEXT("Wiz"));
	UCKSessionsProfileData* Plain = CreateTestEntity(Name);
	if (!TestNotNull(TEXT("The wizard creates the profile"), Plain))
	{
		return false;
	}
	const FString PlainFolder = FString(TestEntitiesRoot) / Name;
	TestEqual(TEXT("The kit is DA_SessionsProfile_<Name> alone"), SessionsWorkspaceTestAssetNamesIn(PlainFolder), TArray<FString>{ TEXT("DA_SessionsProfile_") + Name });
	const UCKSessionsProfileData* Defaults = GetDefault<UCKSessionsProfileData>();
	TestEqual(TEXT("The session type is the plugin's default"), Plain->GetSessionType(), Defaults->GetSessionType());
	TestEqual(TEXT("Max Players is the plugin's default"), Plain->GetMaxPlayers(), Defaults->GetMaxPlayers());
	TestEqual(TEXT("The new profile is valid data"), CKSessions::Editor::ValidateData(*Plain).Result, EDataValidationResult::Valid);
	const CKSessions::Workspace::FCKSessionsEntityKit PlainKit = CKSessions::Workspace::CollectKit(*Plain);
	TestEqual(TEXT("Own: the profile alone"), PlainKit.Own.Num(), 1);
	TestTrue(TEXT("The profile comes first"), !PlainKit.Own.IsEmpty() && PlainKit.Own[0].GetAsset() == Plain);
	TestTrue(TEXT("A profile has no shared parts"), PlainKit.Shared.IsEmpty());
	TestTrue(TEXT("A profile has no parts"), CKSessions::Workspace::CollectParts(*Plain).IsEmpty());
	DeleteTestEntity(*Plain);
	TestTrue(TEXT("Cleanup: the folder is empty"), SessionsWorkspaceTestAssetNamesIn(PlainFolder).IsEmpty());

	// Start From and Session Type: another profile's values, the type as typed.
	UCKSessionsProfileData* Source = CreateTestEntity(MakeTestName(TEXT("Src")));
	if (!TestNotNull(TEXT("The source profile"), Source))
	{
		return false;
	}
	SetTestMaxPlayers(*Source, 12);
	UCKSessionsProfileDataFactory* FromSource = MakeTestWizard();
	FromSource->StartFrom = TSoftObjectPtr<UCKSessionsProfileData>(FSoftObjectPath(Source));
	UCKSessionsProfileData* Copy = CreateTestEntity(MakeTestName(TEXT("Copy")), FromSource);
	if (TestNotNull(TEXT("The wizard creates a profile from the source"), Copy))
	{
		TestEqual(TEXT("Max Players comes from the source"), Copy->GetMaxPlayers(), 12);
		TestEqual(TEXT("The session type comes from the source"), Copy->GetSessionType(), Source->GetSessionType());
		DeleteTestEntity(*Copy);
	}
	UCKSessionsProfileDataFactory* WithType = MakeTestWizard();
	WithType->StartFrom = TSoftObjectPtr<UCKSessionsProfileData>(FSoftObjectPath(Source));
	WithType->SessionType = TEXT("MyGame");
	UCKSessionsProfileData* Typed = CreateTestEntity(MakeTestName(TEXT("Typ")), WithType);
	if (TestNotNull(TEXT("The wizard creates a profile with a type"), Typed))
	{
		TestEqual(TEXT("The typed session type wins over the source's"), Typed->GetSessionType(), FName(TEXT("MyGame")));
		TestEqual(TEXT("The other values still come from the source"), Typed->GetMaxPlayers(), 12);
		DeleteTestEntity(*Typed);
	}
	DeleteTestEntity(*Source);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsWorkspaceOperationsTest, "CoopKit.Sessions.Workspace.EntityOperations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsWorkspaceOperationsTest::RunTest(const FString& Parameters)
{
	using namespace CKSessions::Tests;

	const FScopedTestWorkspace Workspace;
	// Renaming saves the renamed assets (the engine's rename does, as in the Content Browser); deleting them right after
	// may race the Asset Registry's file watcher. The test folder is removed from disk at the end (FScopedTestWorkspace).
	AddExpectedMessagePlain(TEXT("package was marked as deleted in editor"), EAutomationExpectedMessageFlags::Contains, 0);
	const FString NameA = MakeTestName(TEXT("OpsA"));
	const FString NameB = MakeTestName(TEXT("OpsB"));
	const FString NameC = MakeTestName(TEXT("OpsC"));
	UCKSessionsProfileData* EntityA = CreateTestEntity(NameA);
	if (!TestNotNull(TEXT("The wizard creates the profile"), EntityA))
	{
		return false;
	}
	const FString FolderA = FString(TestEntitiesRoot) / NameA;

	// Duplicate: the copy is a profile with the same values in its own folder.
	FText Error;
	UCKSessionsProfileData* EntityB = Cast<UCKSessionsProfileData>(CKSessions::Workspace::DuplicateEntity(*EntityA, NameB, Error));
	if (!TestNotNull(FString::Printf(TEXT("Duplicate (%s)"), *Error.ToString()), EntityB))
	{
		return false;
	}
	const FString FolderB = FString(TestEntitiesRoot) / NameB;
	TestEqual(TEXT("Duplicate: the copy's folder holds the renamed profile"), SessionsWorkspaceTestAssetNamesIn(FolderB), TArray<FString>{ TEXT("DA_SessionsProfile_") + NameB });
	TestEqual(TEXT("Duplicate: the copy has the values of the original"), EntityB->GetSessionType(), EntityA->GetSessionType());

	// Rename: the profile moves to the new folder with the new name; no redirectors stay.
	UCKSessionsProfileData* EntityC = Cast<UCKSessionsProfileData>(CKSessions::Workspace::RenameEntity(*EntityB, NameC, Error));
	if (!TestNotNull(FString::Printf(TEXT("Rename (%s)"), *Error.ToString()), EntityC))
	{
		return false;
	}
	const FString FolderC = FString(TestEntitiesRoot) / NameC;
	TestEqual(TEXT("Rename: the new folder holds the profile with the new name"), SessionsWorkspaceTestAssetNamesIn(FolderC), TArray<FString>{ TEXT("DA_SessionsProfile_") + NameC });
	TestTrue(TEXT("Rename: the old folder is empty (redirectors fixed up)"), SessionsWorkspaceTestAssetNamesIn(FolderB).IsEmpty());
	TestTrue(TEXT("Rename: the profile is the same object"), EntityC == EntityB);
	TestEqual(TEXT("Rename: the entity name follows"), CKSessions::Workspace::GetEntityName(*EntityC), NameC);

	// Delete: the profile goes, the original stays.
	TestTrue(TEXT("Delete"), CKSessions::Workspace::DeleteEntity(*EntityC, Error));
	TestTrue(TEXT("Delete: the profile's folder is empty"), SessionsWorkspaceTestAssetNamesIn(FolderC).IsEmpty());
	TestEqual(TEXT("Delete: the original keeps its asset"), SessionsWorkspaceTestAssetNamesIn(FolderA), TArray<FString>{ TEXT("DA_SessionsProfile_") + NameA });

	// A name that is a word of the asset prefix (DA_SessionsProfile_Profile): only the entity's name changes, not the prefix.
	UCKSessionsProfileData* Profile = CreateTestEntity(TEXT("Profile"));
	if (TestNotNull(TEXT("Profile named Profile"), Profile))
	{
		TestEqual(TEXT("Its asset is DA_SessionsProfile_Profile"), Profile->GetName(), FString(TEXT("DA_SessionsProfile_Profile")));
		TestEqual(TEXT("The entity name is Profile"), CKSessions::Workspace::GetEntityName(*Profile), FString(TEXT("Profile")));
		const FString NameD = MakeTestName(TEXT("OpsD"));
		UCKSessionsProfileData* EntityD = Cast<UCKSessionsProfileData>(CKSessions::Workspace::RenameEntity(*Profile, NameD, Error));
		if (TestNotNull(FString::Printf(TEXT("Rename Profile (%s)"), *Error.ToString()), EntityD))
		{
			TestEqual(TEXT("Rename keeps the asset prefix: DA_SessionsProfile_<New>"),
				SessionsWorkspaceTestAssetNamesIn(FString(TestEntitiesRoot) / NameD), TArray<FString>{ TEXT("DA_SessionsProfile_") + NameD });
			TestEqual(TEXT("The renamed profile keeps its entity name"), CKSessions::Workspace::GetEntityName(*EntityD), NameD);
			DeleteTestEntity(*EntityD);
		}
	}

	// Errors: an invalid or taken name changes nothing.
	TestNull(TEXT("A name with a space is refused"), CKSessions::Workspace::RenameEntity(*EntityA, TEXT("Bad Name"), Error));
	TestNull(TEXT("The current name is refused"), CKSessions::Workspace::DuplicateEntity(*EntityA, NameA, Error));

	DeleteTestEntity(*EntityA);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsWorkspaceCardTest, "CoopKit.Sessions.Workspace.Card",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsWorkspaceCardTest::RunTest(const FString& Parameters)
{
	using namespace CKSessions::Tests;

	const FScopedTestWorkspace Workspace;
	const FString Name = MakeTestName(TEXT("Card"));
	UCKSessionsProfileData* Entity = CreateTestEntity(Name);
	if (!TestNotNull(TEXT("The wizard creates the profile"), Entity))
	{
		return false;
	}

	// Parts tab: a sessions profile refers to no other asset, so the list is empty (the tab says so).
	const TSharedRef<SCKSessionsParts> Parts = SNew(SCKSessionsParts, Entity);
	TestTrue(TEXT("The Parts tab is empty: a profile has no parts"), Parts->GetItems().IsEmpty());

	// Preview tab: nothing to show, the tab does not break.
	const TSharedRef<SCKSessionsPreview> Preview = SNew(SCKSessionsPreview, Entity);
	TestFalse(TEXT("The preview has no asset to show"), Preview->GetPreviewedAsset().IsValid());

	// Hub window: the profile is listed by its asset name (sessions profiles have no display name).
	const TSharedRef<SCKSessionsHub> Hub = SNew(SCKSessionsHub);
	TestTrue(TEXT("The hub lists the profile"), Hub->GetEntries().ContainsByPredicate([Entity](const TSharedPtr<FCKSessionsHubEntry>& Entry)
	{
		return Entry->Asset.GetAsset() == Entity && Entry->Entity == TEXT("Sessions Profile") && Entry->DisplayName.ToString() == Entity->GetName();
	}));

	DeleteTestEntity(*Entity);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsWorkspaceExistingAssetsTest, "CoopKit.Sessions.Workspace.ExistingAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsWorkspaceExistingAssetsTest::RunTest(const FString& Parameters)
{
	using namespace CKSessions::Tests;

	// A profile in the older layout (a buyer's own folder and name), as the Content Browser factory made it before the workspace.
	const FScopedTestWorkspace Workspace;
	UCKSessionsProfileData* Demo = CreateLegacyTestProfile();
	if (!TestNotNull(TEXT("The existing profile is created"), Demo))
	{
		return false;
	}
	CKSessions::Hub::FEntityType Type;
	TestTrue(TEXT("Sessions profile type"), CKSessions::Hub::FindEntityType(UCKSessionsProfileData::StaticClass(), Type));
	TestTrue(TEXT("The hub lists the existing profile where it is"), CKSessions::Hub::FindEntities(Type).ContainsByPredicate([Demo](const FAssetData& Asset) { return Asset.GetAsset() == Demo; }));
	TestEqual(TEXT("Its name is the asset name (no DA_SessionsProfile_ prefix)"), CKSessions::Workspace::GetEntityName(*Demo), FString(LegacyProfileName));

	// A folder not named after the profile holds only what it uses: nothing but the profile itself.
	const CKSessions::Workspace::FCKSessionsEntityKit Kit = CKSessions::Workspace::CollectKit(*Demo);
	TestEqual(TEXT("Own: the profile alone"), Kit.Own.Num(), 1);
	TestTrue(TEXT("The kit is the profile itself"), !Kit.Own.IsEmpty() && Kit.Own[0].GetAsset() == Demo);
	TestEqual(TEXT("The existing profile is valid data"), CKSessions::Editor::ValidateData(*Demo).Result, EDataValidationResult::Valid);

	// "Open": the card, with the Preview and Parts tabs; opening changes nothing.
	UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	TestTrue(TEXT("The hub opens the card"), CKSessions::Hub::OpenCard(Demo));
	IAssetEditorInstance* Instance = AssetEditors->FindEditorForAsset(Demo, false);
	if (TestNotNull(TEXT("Card of the profile"), Instance) && TestEqual(TEXT("The card is the plugin's editor"), Instance->GetEditorName(), FCKSessionsDataEditorToolkit::ToolkitName))
	{
		const TArray<FName> Tabs = static_cast<FCKSessionsDataEditorToolkit*>(Instance)->GetExtraTabIds();
		TestTrue(TEXT("The card has the Preview and Parts tabs"), Tabs.Contains(FName(TEXT("CKSessionsCard_Preview"))) && Tabs.Contains(FName(TEXT("CKSessionsCard_Parts"))));
	}
	TestFalse(TEXT("Opening the card does not dirty the existing profile"), Demo->GetOutermost()->IsDirty());
	DeleteTestEntity(*Demo);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
