// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsSetup.h"
#include "Engine/Engine.h"
#include "GameMapsSettings.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/IProjectManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProjectDescriptor.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Settings/ProjectPackagingSettings.h"

namespace CKSessions::Setup
{
	const FName TabId(TEXT("CoopKitSessionsSetup"));

	namespace
	{
		/** Long package name of an asset path such as /Game/Maps/L_Menu.L_Menu. */
		FString ToPackageName(const FSoftObjectPath& Path)
		{
			return Path.IsNull() ? FString() : Path.GetLongPackageName();
		}
	}

	CKSessions::Diagnostics::FSnapshot GatherProjectSnapshot()
	{
		using namespace CKSessions::Diagnostics;

		FSnapshot Snapshot = GatherSnapshot(nullptr);
		Snapshot.bEditor = true;

		// The packages exist in the editor whatever the packaging settings say: use the cook rules instead.
		const UProjectPackagingSettings* Packaging = GetDefault<UProjectPackagingSettings>();
		TArray<FString> MapsToCook;
		for (const FFilePath& Map : Packaging->MapsToCook)
		{
			MapsToCook.Add(Map.FilePath);
		}
		TArray<FString> Folders;
		for (const FDirectoryPath& Folder : Packaging->DirectoriesToAlwaysCook)
		{
			Folders.Add(Folder.Path);
		}
		const UGameMapsSettings* Maps = GetDefault<UGameMapsSettings>();
		const TArray<FString> AutoCooked = { UGameMapsSettings::GetGameDefaultMap(), ToPackageName(Maps->TransitionMap) };
		for (const FString& Map : FindMapsNotCooked(Snapshot.RequiredMaps, MapsToCook, Folders, Packaging->bCookAll, AutoCooked))
		{
			Snapshot.MissingMaps.AddUnique(Map);
		}
		return Snapshot;
	}

	FString GetProjectEngineIniPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("DefaultEngine.ini")));
	}

	TArray<FString> GetPluginsToEnable(const CKSessions::Diagnostics::EService Service)
	{
		TArray<FString> ToEnable;
		for (const FString& Name : CKSessions::Diagnostics::GetRequiredPlugins(Service, ENGINE_MINOR_VERSION))
		{
			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Name);
			if (Plugin.IsValid() && !Plugin->IsEnabled())
			{
				ToEnable.Add(Name);
			}
		}
		return ToEnable;
	}

	bool ApplyToIniFile(const FString& IniPath, const CKSessions::Diagnostics::EService Service, FString& OutError)
	{
		FString Current;
		if (FPaths::FileExists(IniPath) && !FFileHelper::LoadFileToString(Current, *IniPath))
		{
			OutError = FString::Printf(TEXT("Cannot read %s."), *IniPath);
			return false;
		}
		const FString Updated = CKSessions::Diagnostics::ApplySetupToIni(Current, Service, ENGINE_MINOR_VERSION);
		if (!FFileHelper::SaveStringToFile(Updated, *IniPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Cannot write %s. Is the file read-only or locked by source control?"), *IniPath);
			return false;
		}
		return true;
	}

	bool EnablePlugins(const CKSessions::Diagnostics::EService Service, FString& OutError)
	{
		const TArray<FString> ToEnable = GetPluginsToEnable(Service);
		if (ToEnable.IsEmpty())
		{
			return true;
		}
		for (const FString& Name : ToEnable)
		{
			FText Reason;
			if (!IProjectManager::Get().SetPluginEnabled(Name, true, Reason))
			{
				OutError = FString::Printf(TEXT("Cannot enable the plugin %s: %s"), *Name, *Reason.ToString());
				return false;
			}
		}
		FText Reason;
		if (!IProjectManager::Get().SaveCurrentProjectToDisk(Reason))
		{
			OutError = FString::Printf(TEXT("Cannot save the project file: %s"), *Reason.ToString());
			return false;
		}
		return true;
	}
}
