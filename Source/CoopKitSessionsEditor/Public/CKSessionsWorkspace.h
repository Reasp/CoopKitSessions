// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

class UObject;

/**
 * Everything about an entity as a whole, without any window: its parts in other assets (own or shared), creating and
 * replacing parts, renaming, duplicating and deleting the entity with all its own parts. The card and the New ... wizard
 * are thin views over these functions (decision-63 of the CoopKit line, doc-57).
 *
 * An entity declares its parts with the CoopKitPart metadata on soft object properties:
 *   UPROPERTY(EditAnywhere, meta = (CoopKitPart = "Icon"))                              TSoftObjectPtr<UTexture2D> Icon;
 *   UPROPERTY(EditAnywhere, meta = (CoopKitPart = "Curve", CoopKitPartCreate = "true")) TSoftObjectPtr<UCurveFloat> Curve;
 * The value is the label shown in the card; CoopKitPartCreate makes the card offer Create (a new own asset of the
 * property's class, made by that class's factory). The entity's own data stays in its properties; only references to other
 * assets are parts. Soft object properties without the metadata are not parts.
 */
namespace CKSessions::Workspace
{
	/** One part of an entity in another asset, or an empty slot for one. */
	struct FCKSessionsEntityPart
	{
		/** The soft object property of the entity that holds the part. */
		FName Property;

		/** Shown in the card: the CoopKitPart metadata. */
		FText Label;

		/** The asset; null for an empty slot. */
		FSoftObjectPath Asset;

		/** Class of assets the slot accepts (Replace picks from them). */
		UClass* AssetClass = nullptr;

		/** True when the asset lies in the entity's folder (its own part); false for a shared part or an empty slot. */
		bool bOwn = false;

		/** True when Create can make a new asset for the slot (CoopKitPartCreate and the class has a factory). */
		bool bCanCreate = false;
	};

	/** The entity's own parts (they rename, duplicate and delete with it) and the shared parts it uses (never touched). */
	struct FCKSessionsEntityKit
	{
		/** The entity asset first, then its other own assets. */
		TArray<FAssetData> Own;
		TArray<FSoftObjectPath> Shared;

		/** Assets in the entity's folder that other assets use: treated as shared, reported here. */
		TArray<FText> Warnings;
	};

	/** The entity's name in asset names: the asset name without the DA_<Entity>_ prefix of the hub. */
	COOPKITSESSIONSEDITOR_API FString GetEntityName(const UObject& Entity);

	/** Folder of the entity asset (its package path). */
	COOPKITSESSIONSEDITOR_API FString GetEntityFolder(const UObject& Entity);

	/** Every part and empty slot of the entity, in the order of its properties. */
	COOPKITSESSIONSEDITOR_API TArray<FCKSessionsEntityPart> CollectParts(const UObject& Entity);

	/** Own parts and shared parts of the entity. */
	COOPKITSESSIONSEDITOR_API FCKSessionsEntityKit CollectKit(const UObject& Entity);

	/** Points the part's slot at another asset (undoable). False when the asset does not fit the slot. */
	COOPKITSESSIONSEDITOR_API bool ReplacePart(UObject& Entity, const FCKSessionsEntityPart& Part, UObject* NewAsset);

	/**
	 * Creates a new own asset for the slot in the entity's folder (named <Label>_<EntityName>) and points the slot at it.
	 * Null when the slot cannot be created or the name is taken.
	 */
	COOPKITSESSIONSEDITOR_API UObject* CreatePart(UObject& Entity, const FCKSessionsEntityPart& Part);

	/**
	 * Renames the entity and its own parts: the new folder from the path template, every own asset name with the old entity
	 * name replaced by the new one; redirectors are fixed up; the id and display name follow when they were the old name.
	 * Shared parts stay where they are. Returns the entity, or null with OutError.
	 */
	COOPKITSESSIONSEDITOR_API UObject* RenameEntity(UObject& Entity, const FString& NewName, FText& OutError);

	/**
	 * Copies the entity and its own parts into the new entity's folder; the copy refers to the copies of the own parts and
	 * to the same shared parts. The copy's id is the new name. Returns the copy, or null with OutError.
	 */
	COOPKITSESSIONSEDITOR_API UObject* DuplicateEntity(UObject& Entity, const FString& NewName, FText& OutError);

	/** Deletes the entity and its own parts; shared parts stay. False with OutError when nothing was deleted. */
	COOPKITSESSIONSEDITOR_API bool DeleteEntity(UObject& Entity, FText& OutError);
}
