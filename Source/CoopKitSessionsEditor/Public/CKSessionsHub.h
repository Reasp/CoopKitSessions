// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.
// CoopKit shared file: the same hub code ships in every CoopKit plugin with a hub, under the plugin's prefix. CoopKit shared group: hub.

#pragma once

#include "AssetRegistry/AssetData.h"
#include "Containers/ArrayView.h"
#include "CoreMinimal.h"

class UFactory;

/** Log category of the CoopKitSessions editor module. */
COOPKITSESSIONSEDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogCKSessionsEditor, Log, All);

/**
 * This plugin's copy of the CoopKit Hub (contract version 2, decision-63 of the CoopKit line). Plugins of the line share
 * no code; their copies of the hub agree on names only:
 *   - an entity type is a native class with the CoopKitEntity metadata (CoopKitEntityFolder, CoopKitEntityId optional);
 *   - the tab id is CoopKitHub; the plugin with the highest CoopKitHubVersion marker raises it (ties: smallest class path);
 *   - new entities go to the folder of the [CoopKit.Workspace] path template of the editor config and are named
 *     DA_<Entity>_<Name>;
 *   - "New ..." is the type's factory, "Open" is the type's own asset editor (the card).
 * Version 2 changes only the hub window: categories first, then the entities of one category with a Back button. The
 * entity metadata are the same as in version 1, so a version 2 hub lists the entities of version 1 plugins.
 */
namespace CKSessions::Hub
{
	/** Tab id every CoopKit plugin uses for the hub. */
	extern COOPKITSESSIONSEDITOR_API const FName TabId;

	/** Contract version this copy implements. */
	inline constexpr int32 ContractVersion = 2;

	/** Built-in path templates, used when [CoopKit.Workspace] has no value. */
	inline constexpr const TCHAR* DefaultEntityPathTemplate = TEXT("/Game/CoopKit/{Folder}/{Name}");
	inline constexpr const TCHAR* DefaultSharedPathTemplate = TEXT("/Game/CoopKit/Shared/{Folder}");

	/** An entity type found through class metadata. */
	struct FEntityType
	{
		UClass* Class = nullptr;

		/** CoopKitEntity: the type name shown in the hub, e.g. "Hero". */
		FString Entity;

		/** CoopKitEntityFolder, or the type name plus "s": the {Folder} token of the path templates. */
		FString Folder;

		/** CoopKitEntityId: name of the FName property with the entity's unique id; empty when the type has none. */
		FName IdProperty;

		/** Short name of the class package: the module that declares the type ({Plugin} token). */
		FString Plugin;
	};

	/** A plugin able to raise the hub: its marker class with the CoopKitHubVersion metadata. */
	struct FHubCandidate
	{
		int32 Version = 0;
		FString ClassPath;
	};

	/** One problem the project check found. */
	struct FProjectIssue
	{
		FSoftObjectPath Asset;
		FText Message;
		bool bError = true;
	};

	/** Every loaded native, concrete, current class with the CoopKitEntity metadata, sorted by type name. */
	COOPKITSESSIONSEDITOR_API TArray<FEntityType> CollectEntityTypes();

	/** The entity type of a class (the class itself must carry the metadata); false when it has none. */
	COOPKITSESSIONSEDITOR_API bool FindEntityType(const UClass* Class, FEntityType& OutType);

	/** Every loaded class with a positive CoopKitHubVersion metadata. */
	COOPKITSESSIONSEDITOR_API TArray<FHubCandidate> CollectCandidates();

	/** Index of the hub owner: the highest version, then the smallest class path (case-insensitive); INDEX_NONE if empty. */
	COOPKITSESSIONSEDITOR_API int32 ElectOwner(TConstArrayView<FHubCandidate> Candidates);

	/** Fills the tokens {Plugin} {Entity} {Folder} {Name} of a path template and trims a trailing slash. */
	COOPKITSESSIONSEDITOR_API FString ExpandPathTemplate(const FString& Template, const FEntityType& Type, const FString& Name);

	/** Folder of an entity: EntityPathTemplate.<Entity>, then EntityPathTemplate of [CoopKit.Workspace], then the built-in one. */
	COOPKITSESSIONSEDITOR_API FString ResolveEntityFolder(const FEntityType& Type, const FString& Name);

	/** Folder of the shared parts of a type: SharedPathTemplate of [CoopKit.Workspace], or the built-in one. */
	COOPKITSESSIONSEDITOR_API FString ResolveSharedFolder(const FEntityType& Type);

	/** Asset name of a new entity: DA_<Entity without spaces>_<Name>. */
	COOPKITSESSIONSEDITOR_API FString MakeEntityAssetName(const FEntityType& Type, const FString& Name);

	/** True for a name an entity may have: letters, digits and underscores, starting with a letter, at most 64 characters. */
	COOPKITSESSIONSEDITOR_API bool IsValidEntityName(const FString& Name);

	/** The factory the Content Browser offers for new assets of the class; null when there is none. */
	COOPKITSESSIONSEDITOR_API UFactory* FindCreateFactory(const UClass* Class);

	/**
	 * "New ...": creates the entity through its type's factory in its folder. The factory's wizard (ConfigureProperties)
	 * runs first unless bSkipWizard; a cancelled wizard creates nothing.
	 * @param Factory  Factory to use; null: a new instance of the type's factory.
	 */
	COOPKITSESSIONSEDITOR_API UObject* CreateEntity(const FEntityType& Type, const FString& Name, UFactory* Factory = nullptr, bool bSkipWizard = false);

	/** "Open": the entity in its plugin's own editor (the card). */
	COOPKITSESSIONSEDITOR_API bool OpenCard(UObject* Entity);

	/** Entities of the type known to the Asset Registry, unsaved ones included, sorted by name. */
	COOPKITSESSIONSEDITOR_API TArray<FAssetData> FindEntities(const FEntityType& Type);

	/**
	 * Name of the entity for people: its FText property DisplayName when the type has one and it is set, else the asset
	 * name. Loads the asset.
	 */
	COOPKITSESSIONSEDITOR_API FText GetEntityDisplayName(const FAssetData& Asset);

	/** "Where used": packages that refer to the asset (Asset Registry, saved data). */
	COOPKITSESSIONSEDITOR_API TArray<FName> FindReferencers(const FAssetData& Asset);

	/** Project check: IsDataValid of every entity of every type and duplicate ids. */
	COOPKITSESSIONSEDITOR_API TArray<FProjectIssue> CheckProject();

	/** True while this plugin's copy owns the hub tab. */
	COOPKITSESSIONSEDITOR_API bool OwnsHubTab();

	/**
	 * Registers the hub tab when this plugin wins the election among the loaded plugins and nobody registered it yet.
	 * Call once every plugin module is loaded (OnPostEngineInit). Returns true when this plugin registered the tab.
	 */
	COOPKITSESSIONSEDITOR_API bool RegisterTabIfElected();

	/** Removes the hub tab if this plugin registered it. */
	COOPKITSESSIONSEDITOR_API void UnregisterTabIfOwner();
}
