// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.
// CoopKit shared file: the same agent surface ships in every CoopKit plugin with a hub, under the plugin's prefix. CoopKit shared group: hub.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CKSessionsAgentLibrary.generated.h"

namespace CKSessions::Agent
{
	/** Version of the response schema. A change that breaks a reader gets a new version (decision-129 of the CoopKit line). */
	inline constexpr const TCHAR* SchemaVersion = TEXT("coopkit.agent/1");
}

/**
 * The editor surface of CoopKitSessions for AI agents, Python and Editor Utility Blueprints (decision-129 of the CoopKit line).
 * Every CoopKit plugin with a hub has the same set of operations over its own entity types: what the hub, the New wizard
 * and the card do by mouse is one call here, with no window. Python: unreal.CKSessionsAgentLibrary.list_entity_types().
 *
 * Every function returns one JSON object with a stable shape:
 *   {"schema": "coopkit.agent/1", "plugin": "CoopKitSessions", "operation": "<Name>", "ok": true|false,
 *    "result": <object or null>, "error": null | {"code": "<code>", "message": "<text>"}}
 * Error codes: unknown_type, invalid_name, already_exists, no_factory, create_failed, asset_not_found, not_an_entity,
 * invalid_json, unknown_field, read_only_field, invalid_value, save_failed.
 *
 * A type is named by its entity name (as the hub shows it) or by its class name (with or without the U prefix). An entity is named by its object path (/Game/Folder/DA_Name.DA_Name) or its package path (/Game/Folder/DA_Name).
 * Field values are text in the form of the Details panel's Copy: numbers, True/False, (X=1,Y=2) for structs, asset paths.
 */
UCLASS()
class COOPKITSESSIONSEDITOR_API UCKSessionsAgentLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The plugin, its prefix, the schema version and the list of operations. */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Agent")
	static FString DescribeSurface();

	/** The plugin's entity types: type name, class, folder token, factory of the New wizard, id property. */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Agent")
	static FString ListEntityTypes();

	/** Entities of one type known to the Asset Registry: path, asset name, entity name, display name. */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Agent")
	static FString ListEntities(const FString& TypeName);

	/**
	 * Creates an entity the way New in the hub does (the type's factory with its default options, no wizard window), in
	 * the folder of the [CoopKit.Workspace] path template, named DA_<Entity>_<Name>.
	 * @param TypeName  Entity type or class name.
	 * @param Name      Letters, digits and underscores, starting with a letter.
	 * @param bSave     Save the new asset to disk right away.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Agent")
	static FString CreateEntity(const FString& TypeName, const FString& Name, bool bSave = false);

	/** The entity's editable fields (name, C++ type, category, value, read-only flag) and its parts (CoopKitPart slots). */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Agent")
	static FString DescribeEntity(const FString& AssetPath);

	/** What the Validation tab of the card shows: valid or not, errors and warnings with the field they name when known. */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Agent")
	static FString ValidateEntity(const FString& AssetPath);

	/** Project check of the hub, limited to this plugin's entities: every issue with its asset and severity. */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Agent")
	static FString CheckProject();

	/** The values of the entity's editable fields: {"path": ..., "type": ..., "fields": {"<Field>": "<value>", ...}}. */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Agent")
	static FString ExportEntity(const FString& AssetPath);

	/**
	 * Sets field values from JSON, the shape ExportEntity returns or a flat {"<Field>": "<value>"} object; numbers and
	 * booleans are accepted as JSON values too. All or nothing: an unknown, read-only or bad field changes nothing. The
	 * change is one undo step; the result carries the validation of the new data.
	 * @param bSave  Save the asset to disk after the change.
	 */
	UFUNCTION(BlueprintCallable, Category = "CoopKit|Sessions|Agent")
	static FString ImportEntity(const FString& AssetPath, const FString& Json, bool bSave = false);
};
