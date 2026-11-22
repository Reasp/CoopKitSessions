// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsTestWorkspace.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "CKSessionsProfileData.h"
#include "CKSessionsProfileDataFactory.h"
#include "CKSessionsHub.h"
#include "CKSessionsWorkspace.h"

namespace CKSessions::Tests
{
	namespace
	{
		const TCHAR* const WorkspaceSection = TEXT("CoopKit.Workspace");
		const TCHAR* const EntityTemplateKey = TEXT("EntityPathTemplate.Sessions Profile");
		const TCHAR* const SharedTemplateKey = TEXT("SharedPathTemplate");

		void SetOrRemove(const TCHAR* Key, const FString& Value)
		{
			if (Value.IsEmpty())
			{
				GConfig->RemoveKey(WorkspaceSection, Key, GEditorIni);
			}
			else
			{
				GConfig->SetString(WorkspaceSection, Key, *Value, GEditorIni);
			}
		}
	}

	FScopedTestWorkspace::FScopedTestWorkspace()
	{
		GConfig->GetString(WorkspaceSection, EntityTemplateKey, OldEntityTemplate, GEditorIni);
		GConfig->GetString(WorkspaceSection, SharedTemplateKey, OldSharedTemplate, GEditorIni);
		GConfig->SetString(WorkspaceSection, EntityTemplateKey, TEXT("/Game/CKSessionsTest/{Folder}/{Name}"), GEditorIni);
		GConfig->SetString(WorkspaceSection, SharedTemplateKey, TEXT("/Game/CKSessionsTest/Shared/{Folder}"), GEditorIni);
	}

	FScopedTestWorkspace::~FScopedTestWorkspace()
	{
		SetOrRemove(EntityTemplateKey, OldEntityTemplate);
		SetOrRemove(SharedTemplateKey, OldSharedTemplate);
		// A rename saves the renamed assets: whatever reached the disk under the test folder goes with the test.
		const FString TestFolderOnDisk = FPaths::ProjectContentDir() / TEXT("CKSessionsTest");
		if (IFileManager::Get().DirectoryExists(*TestFolderOnDisk))
		{
			IFileManager::Get().DeleteDirectory(*TestFolderOnDisk, false, true);
		}
	}

	FString MakeTestName(const TCHAR* Prefix)
	{
		return FString(Prefix) + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
	}

	UCKSessionsProfileDataFactory* MakeTestWizard()
	{
		return NewObject<UCKSessionsProfileDataFactory>();
	}

	UCKSessionsProfileData* CreateTestEntity(const FString& Name, UCKSessionsProfileDataFactory* Wizard)
	{
		CKSessions::Hub::FEntityType Type;
		CKSessions::Hub::FindEntityType(UCKSessionsProfileData::StaticClass(), Type);
		return Cast<UCKSessionsProfileData>(CKSessions::Hub::CreateEntity(Type, Name, Wizard != nullptr ? Wizard : MakeTestWizard(), true));
	}

	void SetTestMaxPlayers(UCKSessionsProfileData& Profile, const int32 MaxPlayers)
	{
		FIntProperty* Property = FindFProperty<FIntProperty>(UCKSessionsProfileData::StaticClass(), TEXT("MaxPlayers"));
		check(Property != nullptr);
		Property->SetPropertyValue_InContainer(&Profile, MaxPlayers);
	}

	UCKSessionsProfileData* CreateLegacyTestProfile()
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		UCKSessionsProfileData* Profile = Cast<UCKSessionsProfileData>(
			AssetTools.CreateAsset(LegacyProfileName, LegacyFolder, UCKSessionsProfileData::StaticClass(), NewObject<UCKSessionsProfileDataFactory>()));
		if (Profile != nullptr)
		{
			// Created is not edited: the profile stands as one loaded from disk would.
			Profile->GetOutermost()->ClearDirtyFlag();
		}
		return Profile;
	}

	void DeleteTestEntity(UCKSessionsProfileData& Entity)
	{
		FText Error;
		CKSessions::Workspace::DeleteEntity(Entity, Error);
	}
}
