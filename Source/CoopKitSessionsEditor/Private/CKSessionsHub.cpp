// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.
// CoopKit shared file: the same hub code ships in every CoopKit plugin with a hub, under the plugin's prefix. CoopKit shared group: hub.

#include "CKSessionsHub.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "CKSessionsHubInfo.h"
#include "CKSessionsSkills.h"
#include "Editor.h"
#include "Factories/Factory.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "HAL/IConsoleManager.h"
#include "IAssetTools.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DataValidation.h"
#include "SCKSessionsHub.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "CKSessionsHub"

namespace CKSessions::Hub
{
	const FName TabId(TEXT("CoopKitHub"));

	namespace
	{
		const TCHAR* const WorkspaceSection = TEXT("CoopKit.Workspace");
		const FName EntityKey(TEXT("CoopKitEntity"));
		const FName FolderKey(TEXT("CoopKitEntityFolder"));
		const FName IdKey(TEXT("CoopKitEntityId"));
		const FName VersionKey(TEXT("CoopKitHubVersion"));

		/** True while this copy registered the tab. */
		bool bOwnsTab = false;

		TAutoConsoleVariable<int32> CVarOwnsHub(TEXT("ck.sessions.OwnsHub"), 0,
			TEXT("Set by CoopKitSessions: 1 when its copy of the CoopKit Hub raised the CoopKitHub tab. For tests of the hub election."));

		FString ReadWorkspaceTemplate(const FString& Key)
		{
			FString Value;
			if (GConfig != nullptr)
			{
				GConfig->GetString(WorkspaceSection, *Key, Value, GEditorIni);
			}
			return Value.TrimStartAndEnd();
		}

		TSharedRef<SDockTab> SpawnHubTab(const FSpawnTabArgs& Args)
		{
			return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SCKSessionsHub)];
		}
	}

	TArray<FEntityType> CollectEntityTypes()
	{
		TArray<FEntityType> Types;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			FEntityType Type;
			if (FindEntityType(*It, Type))
			{
				Types.Add(Type);
			}
		}
		Types.Sort([](const FEntityType& A, const FEntityType& B) { return A.Entity < B.Entity; });
		return Types;
	}

	bool FindEntityType(const UClass* Class, FEntityType& OutType)
	{
#if WITH_METADATA
		if (Class == nullptr || !Class->HasAnyClassFlags(CLASS_Native) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			return false;
		}
		const FString* Entity = Class->FindMetaData(EntityKey);
		if (Entity == nullptr || Entity->IsEmpty())
		{
			return false;
		}
		OutType.Class = const_cast<UClass*>(Class);
		OutType.Entity = *Entity;
		const FString* Folder = Class->FindMetaData(FolderKey);
		OutType.Folder = Folder != nullptr && !Folder->IsEmpty() ? *Folder : *Entity + TEXT("s");
		const FString* Id = Class->FindMetaData(IdKey);
		OutType.IdProperty = Id != nullptr ? FName(**Id) : NAME_None;
		OutType.Plugin = FPackageName::GetShortName(Class->GetOutermost()->GetName());
		return true;
#else
		return false;
#endif
	}

	TArray<FHubCandidate> CollectCandidates()
	{
		TArray<FHubCandidate> Candidates;
#if WITH_METADATA
		for (TObjectIterator<UClass> It; It; ++It)
		{
			const FString* Version = It->FindMetaData(VersionKey);
			const int32 Number = Version != nullptr ? FCString::Atoi(**Version) : 0;
			if (Number > 0)
			{
				Candidates.Add({ Number, It->GetPathName() });
			}
		}
#endif
		return Candidates;
	}

	int32 ElectOwner(const TConstArrayView<FHubCandidate> Candidates)
	{
		int32 Best = INDEX_NONE;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			if (Best == INDEX_NONE || Candidates[Index].Version > Candidates[Best].Version
				|| (Candidates[Index].Version == Candidates[Best].Version && Candidates[Index].ClassPath.Compare(Candidates[Best].ClassPath, ESearchCase::IgnoreCase) < 0))
			{
				Best = Index;
			}
		}
		return Best;
	}

	FString ExpandPathTemplate(const FString& Template, const FEntityType& Type, const FString& Name)
	{
		FString Path = Template;
		Path.ReplaceInline(TEXT("{Plugin}"), *Type.Plugin, ESearchCase::CaseSensitive);
		Path.ReplaceInline(TEXT("{Entity}"), *Type.Entity.Replace(TEXT(" "), TEXT("")), ESearchCase::CaseSensitive);
		Path.ReplaceInline(TEXT("{Folder}"), *Type.Folder, ESearchCase::CaseSensitive);
		Path.ReplaceInline(TEXT("{Name}"), *Name, ESearchCase::CaseSensitive);
		while (Path.Len() > 1 && Path.EndsWith(TEXT("/")))
		{
			Path.LeftChopInline(1);
		}
		return Path;
	}

	FString ResolveEntityFolder(const FEntityType& Type, const FString& Name)
	{
		FString Template = ReadWorkspaceTemplate(TEXT("EntityPathTemplate.") + Type.Entity);
		if (Template.IsEmpty())
		{
			Template = ReadWorkspaceTemplate(TEXT("EntityPathTemplate"));
		}
		return ExpandPathTemplate(Template.IsEmpty() ? FString(DefaultEntityPathTemplate) : Template, Type, Name);
	}

	FString ResolveSharedFolder(const FEntityType& Type)
	{
		const FString Template = ReadWorkspaceTemplate(TEXT("SharedPathTemplate"));
		return ExpandPathTemplate(Template.IsEmpty() ? FString(DefaultSharedPathTemplate) : Template, Type, FString());
	}

	FString MakeEntityAssetName(const FEntityType& Type, const FString& Name)
	{
		return FString::Printf(TEXT("DA_%s_%s"), *Type.Entity.Replace(TEXT(" "), TEXT("")), *Name);
	}

	bool IsValidEntityName(const FString& Name)
	{
		if (Name.IsEmpty() || Name.Len() > 64 || !FChar::IsAlpha(Name[0]))
		{
			return false;
		}
		for (const TCHAR Char : Name)
		{
			if (!FChar::IsAlnum(Char) && Char != TEXT('_'))
			{
				return false;
			}
		}
		return true;
	}

	UFactory* FindCreateFactory(const UClass* Class)
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (!It->IsChildOf(UFactory::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract))
			{
				continue;
			}
			UFactory* Factory = It->GetDefaultObject<UFactory>();
			if (Factory->CanCreateNew() && Factory->GetSupportedClass() == Class)
			{
				return Factory;
			}
		}
		return nullptr;
	}

	UObject* CreateEntity(const FEntityType& Type, const FString& Name, UFactory* Factory, const bool bSkipWizard)
	{
		if (!IsValidEntityName(Name))
		{
			return nullptr;
		}
		if (Factory == nullptr)
		{
			const UFactory* Template = FindCreateFactory(Type.Class);
			Factory = Template != nullptr ? NewObject<UFactory>(GetTransientPackage(), Template->GetClass()) : nullptr;
		}
		if (Factory == nullptr || (!bSkipWizard && !Factory->ConfigureProperties()))
		{
			return nullptr;
		}
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		// Four arguments: UE 5.8 added an optional bOverwriteExisting, so this call is the same on every supported version.
		return AssetTools.CreateAsset(MakeEntityAssetName(Type, Name), ResolveEntityFolder(Type, Name), Type.Class, Factory);
	}

	bool OpenCard(UObject* Entity)
	{
		return Entity != nullptr && GEditor != nullptr && GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Entity);
	}

	TArray<FAssetData> FindEntities(const FEntityType& Type)
	{
		TArray<FAssetData> Assets;
		if (Type.Class == nullptr)
		{
			return Assets;
		}
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		Registry.GetAssetsByClass(Type.Class->GetClassPathName(), Assets, true);
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
		return Assets;
	}

	FText GetEntityDisplayName(const FAssetData& Asset)
	{
		const UObject* Entity = Asset.GetAsset();
		const FTextProperty* Property = Entity != nullptr ? FindFProperty<FTextProperty>(Entity->GetClass(), TEXT("DisplayName")) : nullptr;
		const FText Name = Property != nullptr ? Property->GetPropertyValue_InContainer(Entity) : FText::GetEmpty();
		return Name.IsEmptyOrWhitespace() ? FText::FromName(Asset.AssetName) : Name;
	}

	TArray<FName> FindReferencers(const FAssetData& Asset)
	{
		TArray<FName> Referencers;
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		Registry.GetReferencers(Asset.PackageName, Referencers);
		Referencers.Remove(Asset.PackageName);
		Referencers.Sort(FNameLexicalLess());
		return Referencers;
	}

	TArray<FProjectIssue> CheckProject()
	{
		TArray<FProjectIssue> Issues;
		for (const FEntityType& Type : CollectEntityTypes())
		{
			TMap<FName, FSoftObjectPath> Ids;
			const FNameProperty* IdProperty = Type.IdProperty.IsNone() ? nullptr : FindFProperty<FNameProperty>(Type.Class, Type.IdProperty);
			for (const FAssetData& Data : FindEntities(Type))
			{
				UObject* Entity = Data.GetAsset();
				if (Entity == nullptr)
				{
					Issues.Add({ Data.GetSoftObjectPath(), LOCTEXT("NotLoaded", "The asset does not load."), true });
					continue;
				}
				FDataValidationContext Context;
				// The const overload: the non-const one is deprecated (C4996 with strict warnings).
				static_cast<const UObject*>(Entity)->IsDataValid(Context);
				for (const FDataValidationContext::FIssue& Issue : Context.GetIssues())
				{
					Issues.Add({ Data.GetSoftObjectPath(), Issue.Message, Issue.Severity == EMessageSeverity::Error });
				}
				if (IdProperty != nullptr)
				{
					const FName Id = IdProperty->GetPropertyValue_InContainer(Entity);
					if (const FSoftObjectPath* Other = Id.IsNone() ? nullptr : Ids.Find(Id))
					{
						Issues.Add({ Data.GetSoftObjectPath(), FText::Format(LOCTEXT("DuplicateId", "{0} {1} is also the id of {2}."), FText::FromString(Type.Entity),
							FText::FromName(Id), FText::FromString(Other->GetAssetName())), true });
					}
					else if (!Id.IsNone())
					{
						Ids.Add(Id, Data.GetSoftObjectPath());
					}
				}
			}
		}
		return Issues;
	}

	bool OwnsHubTab()
	{
		return bOwnsTab;
	}

	bool RegisterTabIfElected()
	{
		if (bOwnsTab || IsRunningCommandlet() || !FSlateApplication::IsInitialized())
		{
			return bOwnsTab;
		}
		const TArray<FHubCandidate> Candidates = CollectCandidates();
		const int32 Owner = ElectOwner(Candidates);
		if (Owner == INDEX_NONE || Candidates[Owner].ClassPath != UCKSessionsHubInfo::StaticClass()->GetPathName())
		{
			UE_LOG(LogCKSessionsEditor, Verbose, TEXT("CoopKit Hub: owner is %s; CoopKitSessions (contract v%d) only publishes its entity types."),
				Owner != INDEX_NONE ? *Candidates[Owner].ClassPath : TEXT("none"), ContractVersion);
			return false;
		}
		if (FGlobalTabmanager::Get()->HasTabSpawner(TabId))
		{
			return false;
		}
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabId, FOnSpawnTab::CreateStatic(&SpawnHubTab))
			.SetDisplayName(LOCTEXT("HubTitle", "CoopKit Hub"))
			.SetTooltipText(LOCTEXT("HubTooltip", "Every entity of every installed CoopKit plugin in one place: find, open, create, check."))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());
		bOwnsTab = true;
		CVarOwnsHub->Set(1, ECVF_SetByCode);
		// One menu entry for the whole line: only the hub owner adds Tools > CoopKit > Install AI Agent Skills.
		Skills::RegisterMenu();
		UE_LOG(LogCKSessionsEditor, Display, TEXT("CoopKit Hub: tab registered by CoopKitSessions (contract v%d, %d plugins)."), ContractVersion, Candidates.Num());
		return true;
	}

	void UnregisterTabIfOwner()
	{
		if (bOwnsTab)
		{
			Skills::UnregisterMenu();
		}
		if (bOwnsTab && FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabId);
		}
		bOwnsTab = false;
		CVarOwnsHub->Set(0, ECVF_SetByCode);
	}
}

#undef LOCTEXT_NAMESPACE
