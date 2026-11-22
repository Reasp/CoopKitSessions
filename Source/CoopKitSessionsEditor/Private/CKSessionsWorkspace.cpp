// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsWorkspace.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "Engine/Blueprint.h"
#include "Factories/Factory.h"
#include "IAssetTools.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "CKSessionsHub.h"

#define LOCTEXT_NAMESPACE "CKSessionsWorkspace"

namespace CKSessions::Workspace
{
	namespace
	{
		const FName PartKey(TEXT("CoopKitPart"));
		const FName PartCreateKey(TEXT("CoopKitPartCreate"));

		IAssetTools& GetAssetTools()
		{
			return FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		}

		IAssetRegistry& GetAssetRegistry()
		{
			return FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		}

		bool IsInFolder(const FString& PackageName, const FString& Folder)
		{
			return PackageName.StartsWith(Folder + TEXT("/"));
		}

		/** The hub's entity type of the object's class; Type.Class stays null for a class without the CoopKitEntity metadata. */
		Hub::FEntityType TypeOf(const UObject& Entity)
		{
			Hub::FEntityType Type;
			Hub::FindEntityType(Entity.GetClass(), Type);
			return Type;
		}

		/** The asset-name prefix of the entity type: DA_<Entity without spaces>_ . */
		FString AssetPrefixOf(const UObject& Entity)
		{
			const Hub::FEntityType Type = TypeOf(Entity);
			return Type.Class != nullptr ? Hub::MakeEntityAssetName(Type, FString()) : FString();
		}

		/** Runs Change between PreEditChange and PostEditChangeProperty of the property, as the Details panel does. */
		template <typename InChangeType>
		void EditProperty(UObject& Object, FProperty& Property, InChangeType&& Change)
		{
			Object.Modify();
			Object.PreEditChange(&Property);
			Change();
			FPropertyChangedEvent Event(&Property, EPropertyChangeType::ValueSet);
			Object.PostEditChangeProperty(Event);
		}

		/** True when the folder already has a package or an asset with the name. */
		bool IsAssetNameTaken(const FString& Folder, const FString& Name)
		{
			const FString PackageName = Folder / Name;
			return FindPackage(nullptr, *PackageName) != nullptr || GetAssetRegistry().GetAssetByObjectPath(FSoftObjectPath(PackageName + TEXT(".") + Name)).IsValid();
		}

		/** Checks the new name of a rename or a duplicate; fills OutError and returns false when it cannot be used. */
		bool CheckNewName(const UObject& Entity, const FString& NewName, FText& OutError)
		{
			if (!Hub::IsValidEntityName(NewName))
			{
				OutError = FText::Format(LOCTEXT("BadName", "\"{0}\" is not a valid entity name: use letters, digits and underscores, starting with a letter."), FText::FromString(NewName));
				return false;
			}
			if (NewName == GetEntityName(Entity))
			{
				OutError = LOCTEXT("SameName", "The new name is the entity's current name.");
				return false;
			}
			const Hub::FEntityType Type = TypeOf(Entity);
			if (Type.Class == nullptr)
			{
				OutError = LOCTEXT("NoType", "The class has no CoopKitEntity metadata: it is not an entity.");
				return false;
			}
			const FString AssetName = Hub::MakeEntityAssetName(Type, NewName);
			const FString PackageName = Hub::ResolveEntityFolder(Type, NewName) / AssetName;
			if (IsAssetNameTaken(Hub::ResolveEntityFolder(Type, NewName), AssetName))
			{
				OutError = FText::Format(LOCTEXT("Taken", "{0} already exists."), FText::FromString(PackageName));
				return false;
			}
			return true;
		}

		/** Deletes the redirectors left in the folder after a rename, pointing their referencers at the new assets. */
		void FixupRedirectorsIn(const FString& Folder)
		{
			TArray<UObjectRedirector*> Redirectors;
			for (TObjectIterator<UObjectRedirector> It; It; ++It)
			{
				if (IsInFolder(It->GetOutermost()->GetName(), Folder) && !It->HasAnyFlags(RF_ClassDefaultObject))
				{
					Redirectors.Add(*It);
				}
			}
			TArray<FAssetData> OnDisk;
			GetAssetRegistry().GetAssetsByPath(FName(*Folder), OnDisk, true);
			for (const FAssetData& Data : OnDisk)
			{
				if (Data.IsRedirector())
				{
					if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(Data.GetAsset()))
					{
						Redirectors.AddUnique(Redirector);
					}
				}
			}
			if (!Redirectors.IsEmpty())
			{
				GetAssetTools().FixupReferencers(Redirectors, false, ERedirectFixupMode::DeleteFixedUpRedirectors);
			}
		}

		/** Relative sub folder of an asset inside the entity's folder ("" or "/Sub"). */
		FString SubFolderOf(const FAssetData& Asset, const FString& Folder)
		{
			const FString Path = Asset.PackagePath.ToString();
			return Path.Len() > Folder.Len() && Path.StartsWith(Folder) ? Path.Mid(Folder.Len()) : FString();
		}

		/**
		 * The asset name with the entity's old name replaced by the new one: the first occurrence after the asset's type prefix
		 * (up to the first underscore) that stands as a whole word, between underscores or at the end. The old name inside the
		 * prefix or a suffix stays: DA_Hero_Hero renamed to Tank is DA_Hero_Tank, AM_Step_Step1 renamed to Kick is
		 * AM_Kick_Step1. A name without such an occurrence (an asset of an older layout) gets every occurrence replaced.
		 */
		FString RenamedAssetName(const FAssetData& Asset, const FString& OldName, const FString& NewName)
		{
			const FString Name = Asset.AssetName.ToString();
			const int32 PrefixEnd = Name.Find(TEXT("_"), ESearchCase::CaseSensitive);
			int32 From = PrefixEnd == INDEX_NONE ? 0 : PrefixEnd + 1;
			while (!OldName.IsEmpty())
			{
				const int32 Found = Name.Find(OldName, ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
				if (Found == INDEX_NONE)
				{
					break;
				}
				const int32 End = Found + OldName.Len();
				if ((Found == 0 || Name[Found - 1] == TEXT('_')) && (End == Name.Len() || Name[End] == TEXT('_')))
				{
					return Name.Left(Found) + NewName + Name.Mid(End);
				}
				From = Found + 1;
			}
			return Name.Replace(*OldName, *NewName, ESearchCase::CaseSensitive);
		}

		/** After a rename or a duplicate: the id (always for a copy) and the display name follow the new name when they were the old one. */
		void SetIdAndNameAfterRename(UObject& Entity, const FString& OldName, const FString& NewName, const bool bAlwaysSetId)
		{
			const Hub::FEntityType Type = TypeOf(Entity);
			FNameProperty* IdProperty = Type.IdProperty.IsNone() ? nullptr : FindFProperty<FNameProperty>(Entity.GetClass(), Type.IdProperty);
			if (IdProperty != nullptr && (bAlwaysSetId || IdProperty->GetPropertyValue_InContainer(&Entity) == FName(*OldName)))
			{
				EditProperty(Entity, *IdProperty, [&]() { IdProperty->SetPropertyValue_InContainer(&Entity, FName(*NewName)); });
			}
			FTextProperty* NameProperty = FindFProperty<FTextProperty>(Entity.GetClass(), TEXT("DisplayName"));
			if (NameProperty != nullptr && NameProperty->GetPropertyValue_InContainer(&Entity).ToString() == OldName)
			{
				EditProperty(Entity, *NameProperty, [&]() { NameProperty->SetPropertyValue_InContainer(&Entity, FText::FromString(NewName)); });
			}
		}

		FSoftObjectProperty* FindPartProperty(const UObject& Entity, const FName Name)
		{
			FSoftObjectProperty* Property = CastField<FSoftObjectProperty>(Entity.GetClass()->FindPropertyByName(Name));
			return Property != nullptr && !Property->IsA<FSoftClassProperty>() ? Property : nullptr;
		}
	}

	FString GetEntityName(const UObject& Entity)
	{
		FString Name = Entity.GetName();
		const FString Prefix = AssetPrefixOf(Entity);
		if (!Prefix.IsEmpty())
		{
			Name.RemoveFromStart(Prefix, ESearchCase::CaseSensitive);
		}
		return Name;
	}

	FString GetEntityFolder(const UObject& Entity)
	{
		return FPackageName::GetLongPackagePath(Entity.GetOutermost()->GetName());
	}

	TArray<FCKSessionsEntityPart> CollectParts(const UObject& Entity)
	{
		TArray<FCKSessionsEntityPart> Parts;
#if WITH_METADATA
		const FString Folder = GetEntityFolder(Entity);
		for (TFieldIterator<FSoftObjectProperty> It(Entity.GetClass()); It; ++It)
		{
			const FSoftObjectProperty* Property = *It;
			if (Property->IsA<FSoftClassProperty>() || !Property->HasMetaData(PartKey))
			{
				continue;
			}
			FCKSessionsEntityPart Part;
			Part.Property = Property->GetFName();
			Part.Label = FText::FromString(Property->GetMetaData(PartKey));
			Part.Asset = Property->GetPropertyValue_InContainer(&Entity).ToSoftObjectPath();
			Part.AssetClass = Property->PropertyClass;
			Part.bOwn = !Part.Asset.IsNull() && IsInFolder(Part.Asset.GetLongPackageName(), Folder);
			Part.bCanCreate = Property->GetBoolMetaData(PartCreateKey) && Hub::FindCreateFactory(Property->PropertyClass) != nullptr;
			Parts.Add(Part);
		}
#endif
		return Parts;
	}

	FCKSessionsEntityKit CollectKit(const UObject& Entity)
	{
		FCKSessionsEntityKit Kit;
		const FString Folder = GetEntityFolder(Entity);
		IAssetRegistry& Registry = GetAssetRegistry();
		TArray<FAssetData> InFolder;
		Registry.GetAssetsByPath(FName(*Folder), InFolder, true);

		const FName EntityPackage = Entity.GetOutermost()->GetFName();
		TSet<FName> PartPackages;
		const TArray<FCKSessionsEntityPart> Parts = CollectParts(Entity);
		for (const FCKSessionsEntityPart& Part : Parts)
		{
			if (!Part.Asset.IsNull())
			{
				PartPackages.Add(FName(*Part.Asset.GetLongPackageName()));
			}
		}

		// An entity's own folder holds its kit: it is named after the entity ({Folder}/{Name} of the path templates) and holds no
		// other entity of the type. Any other folder (an older layout that keeps a plugin's data together, or a folder shared by
		// several entities) holds only what this entity uses.
		const bool bSharedFolder = FPaths::GetCleanFilename(Folder) != GetEntityName(Entity)
			|| InFolder.ContainsByPredicate([&Entity, EntityPackage](const FAssetData& Data)
			{
				return Data.PackageName != EntityPackage && Data.GetClass() == Entity.GetClass();
			});
		TArray<FAssetData> Candidates;
		TSet<FName> CandidatePackages;
		CandidatePackages.Add(EntityPackage);
		for (const FAssetData& Data : InFolder)
		{
			if (Data.PackageName == EntityPackage || Data.IsRedirector() || (bSharedFolder && !PartPackages.Contains(Data.PackageName)))
			{
				continue;
			}
			Candidates.Add(Data);
			CandidatePackages.Add(Data.PackageName);
		}

		Kit.Own.Add(FAssetData(&Entity));
		TSet<FName> OwnPackages;
		OwnPackages.Add(EntityPackage);
		for (const FAssetData& Data : Candidates)
		{
			TArray<FName> Referencers;
			Registry.GetReferencers(Data.PackageName, Referencers);
			const FName* Outside = Referencers.FindByPredicate([&CandidatePackages](const FName& Referencer) { return !CandidatePackages.Contains(Referencer); });
			if (Outside != nullptr)
			{
				Kit.Warnings.Add(FText::Format(LOCTEXT("UsedOutside", "{0} lies in the entity's folder but {1} uses it too: it is treated as shared."),
					FText::FromName(Data.AssetName), FText::FromName(*Outside)));
				Kit.Shared.AddUnique(Data.GetSoftObjectPath());
				continue;
			}
			Kit.Own.Add(Data);
			OwnPackages.Add(Data.PackageName);
		}
		for (const FCKSessionsEntityPart& Part : Parts)
		{
			if (!Part.Asset.IsNull() && !OwnPackages.Contains(FName(*Part.Asset.GetLongPackageName())))
			{
				Kit.Shared.AddUnique(Part.Asset);
			}
		}
		return Kit;
	}

	bool ReplacePart(UObject& Entity, const FCKSessionsEntityPart& Part, UObject* NewAsset)
	{
		FSoftObjectProperty* Property = FindPartProperty(Entity, Part.Property);
		if (Property == nullptr || (NewAsset != nullptr && !NewAsset->IsA(Property->PropertyClass)))
		{
			return false;
		}
		const FScopedTransaction Transaction(LOCTEXT("ReplacePart", "Replace Entity Part"));
		EditProperty(Entity, *Property, [&]() { Property->SetPropertyValue_InContainer(&Entity, FSoftObjectPtr(NewAsset)); });
		return true;
	}

	UObject* CreatePart(UObject& Entity, const FCKSessionsEntityPart& Part)
	{
		if (!Part.bCanCreate || Part.AssetClass == nullptr)
		{
			return nullptr;
		}
		const UFactory* Template = Hub::FindCreateFactory(Part.AssetClass);
		if (Template == nullptr)
		{
			return nullptr;
		}
		const FString Folder = GetEntityFolder(Entity);
		const FString Name = FString::Printf(TEXT("%s_%s"), *Part.Label.ToString().Replace(TEXT(" "), TEXT("")), *GetEntityName(Entity));
		if (IsAssetNameTaken(Folder, Name))
		{
			return nullptr;
		}
		UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), Template->GetClass());
		UObject* Created = GetAssetTools().CreateAsset(Name, Folder, Part.AssetClass, Factory);
		if (Created != nullptr)
		{
			ReplacePart(Entity, Part, Created);
		}
		return Created;
	}

	UObject* RenameEntity(UObject& Entity, const FString& NewName, FText& OutError)
	{
		if (!CheckNewName(Entity, NewName, OutError))
		{
			return nullptr;
		}
		const Hub::FEntityType Type = TypeOf(Entity);
		const FString OldName = GetEntityName(Entity);
		const FString OldFolder = GetEntityFolder(Entity);
		const FString NewFolder = Hub::ResolveEntityFolder(Type, NewName);
		const FCKSessionsEntityKit Kit = CollectKit(Entity);
		TArray<FAssetRenameData> Renames;
		TArray<TPair<UObject*, FSoftObjectPath>> OldPaths;
		for (const FAssetData& Asset : Kit.Own)
		{
			UObject* Object = Asset.GetAsset();
			if (Object != nullptr)
			{
				Renames.Emplace(Object, NewFolder + SubFolderOf(Asset, OldFolder), RenamedAssetName(Asset, OldName, NewName));
				OldPaths.Emplace(Object, FSoftObjectPath(Object));
				if (const UBlueprint* Blueprint = Cast<UBlueprint>(Object))
				{
					if (Blueprint->GeneratedClass != nullptr)
					{
						OldPaths.Emplace(Blueprint->GeneratedClass, FSoftObjectPath(Blueprint->GeneratedClass));
					}
				}
			}
		}
		if (!GetAssetTools().RenameAssets(Renames))
		{
			OutError = LOCTEXT("RenameFailed", "The engine refused to rename the entity's assets (see the Message Log).");
			return nullptr;
		}
		// Soft references inside the kit are fixed here too: the engine fixes the referencers it finds in the Asset Registry,
		// which does not know the references of packages that were never saved.
		TMap<FSoftObjectPath, FSoftObjectPath> SoftReplacements;
		TArray<UPackage*> KitPackages;
		for (const TPair<UObject*, FSoftObjectPath>& Old : OldPaths)
		{
			SoftReplacements.Add(Old.Value, FSoftObjectPath(Old.Key));
			KitPackages.AddUnique(Old.Key->GetOutermost());
		}
		GetAssetTools().RenameReferencingSoftObjectPaths(KitPackages, SoftReplacements);
		FixupRedirectorsIn(OldFolder);
		SetIdAndNameAfterRename(Entity, OldName, NewName, false);
		return &Entity;
	}

	UObject* DuplicateEntity(UObject& Entity, const FString& NewName, FText& OutError)
	{
		if (!CheckNewName(Entity, NewName, OutError))
		{
			return nullptr;
		}
		const Hub::FEntityType Type = TypeOf(Entity);
		const FString OldName = GetEntityName(Entity);
		const FString OldFolder = GetEntityFolder(Entity);
		const FString NewFolder = Hub::ResolveEntityFolder(Type, NewName);
		const FCKSessionsEntityKit Kit = CollectKit(Entity);
		IAssetTools& AssetTools = GetAssetTools();

		TMap<UObject*, UObject*> Replacements;
		TMap<FSoftObjectPath, FSoftObjectPath> SoftReplacements;
		TArray<UObject*> Copies;
		UObject* EntityCopy = nullptr;
		for (const FAssetData& Asset : Kit.Own)
		{
			UObject* Original = Asset.GetAsset();
			UObject* Copy = Original != nullptr ? AssetTools.DuplicateAsset(RenamedAssetName(Asset, OldName, NewName), NewFolder + SubFolderOf(Asset, OldFolder), Original) : nullptr;
			if (Copy == nullptr)
			{
				OutError = FText::Format(LOCTEXT("CopyFailed", "Could not copy {0}."), FText::FromName(Asset.AssetName));
				return nullptr;
			}
			Copies.Add(Copy);
			Replacements.Add(Original, Copy);
			SoftReplacements.Add(FSoftObjectPath(Original), FSoftObjectPath(Copy));
			const UBlueprint* OriginalBlueprint = Cast<UBlueprint>(Original);
			const UBlueprint* CopyBlueprint = Cast<UBlueprint>(Copy);
			if (OriginalBlueprint != nullptr && CopyBlueprint != nullptr && OriginalBlueprint->GeneratedClass != nullptr && CopyBlueprint->GeneratedClass != nullptr)
			{
				Replacements.Add(OriginalBlueprint->GeneratedClass, CopyBlueprint->GeneratedClass);
				SoftReplacements.Add(FSoftObjectPath(OriginalBlueprint->GeneratedClass), FSoftObjectPath(CopyBlueprint->GeneratedClass));
			}
			if (Original == &Entity)
			{
				EntityCopy = Copy;
			}
		}

		// The copies still point at the original own parts: hard references and soft ones.
		TArray<UPackage*> CopyPackages;
		for (UObject* Copy : Copies)
		{
			FArchiveReplaceObjectRef<UObject> ReplaceRefs(Copy, Replacements, EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::IgnoreArchetypeRef);
			CopyPackages.AddUnique(Copy->GetOutermost());
			Copy->MarkPackageDirty();
		}
		AssetTools.RenameReferencingSoftObjectPaths(CopyPackages, SoftReplacements);

		if (EntityCopy == nullptr)
		{
			OutError = LOCTEXT("NoCopy", "The entity asset was not copied.");
			return nullptr;
		}
		SetIdAndNameAfterRename(*EntityCopy, OldName, NewName, true);
		return EntityCopy;
	}

	bool DeleteEntity(UObject& Entity, FText& OutError)
	{
		const FString Folder = GetEntityFolder(Entity);
		const FCKSessionsEntityKit Kit = CollectKit(Entity);
		TArray<UObject*> Objects;
		for (const FAssetData& Asset : Kit.Own)
		{
			if (UObject* Object = Asset.GetAsset())
			{
				Objects.Add(Object);
			}
		}
		const int32 Deleted = ObjectTools::ForceDeleteObjects(Objects, false);
		if (Deleted == 0)
		{
			OutError = LOCTEXT("DeleteFailed", "Nothing was deleted (see the Message Log).");
			return false;
		}
		TArray<FAssetData> Left;
		GetAssetRegistry().GetAssetsByPath(FName(*Folder), Left, true);
		if (Left.IsEmpty())
		{
			GetAssetRegistry().RemovePath(Folder);
		}
		return true;
	}
}

#undef LOCTEXT_NAMESPACE
