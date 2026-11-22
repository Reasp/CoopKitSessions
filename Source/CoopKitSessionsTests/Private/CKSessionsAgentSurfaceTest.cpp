// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.
// CoopKit shared file: the same agent surface test ships in every CoopKit plugin with a hub, under the plugin's prefix. CoopKit shared group: hub.

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"
#include "CKSessionsAgentLibrary.h"
#include "CKSessionsDataValidation.h"
#include "CKSessionsHub.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CKSessions::Tests::Agent
{
	namespace
	{
		/** Folder the agent surface test creates entities in; the test deletes it, saved files included. */
		const TCHAR* const AgentTestRoot = TEXT("/Game/CKSessionsAgentTest");

		TSharedPtr<FJsonObject> Parse(const FString& Text)
		{
			TSharedPtr<FJsonObject> Object;
			const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Text);
			return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
		}

		FString ToText(const TSharedPtr<FJsonObject>& Object)
		{
			FString Out;
			const TSharedRef<TJsonWriter<TCHAR>> Writer = TJsonWriterFactory<TCHAR>::Create(&Out);
			FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
			return Out;
		}

		/** The result object of a successful response; null for a failed or malformed one. */
		TSharedPtr<FJsonObject> ResultOf(const TSharedPtr<FJsonObject>& Response)
		{
			const TSharedPtr<FJsonObject>* Result = nullptr;
			return Response.IsValid() && Response->GetBoolField(TEXT("ok")) && Response->TryGetObjectField(TEXT("result"), Result) && Result != nullptr ? *Result : nullptr;
		}

		/** The error code of a failed response; empty for a successful one. */
		FString ErrorCodeOf(const FString& Text)
		{
			const TSharedPtr<FJsonObject> Response = Parse(Text);
			const TSharedPtr<FJsonObject>* Error = nullptr;
			return Response.IsValid() && !Response->GetBoolField(TEXT("ok")) && Response->TryGetObjectField(TEXT("error"), Error) && Error != nullptr
				? (*Error)->GetStringField(TEXT("code")) : FString();
		}

		/**
		 * Points the path template of one entity type at the test folder. Restore puts the old value back and deletes the
		 * folder; the test calls it from a latent command a few seconds later, after the Asset Registry has seen the file the
		 * test saved (deleting it first makes the registry warn that a deleted package came back on disk).
		 */
		class FAgentWorkspace
		{
		public:
			explicit FAgentWorkspace(const FString& Entity)
				: Key(TEXT("EntityPathTemplate.") + Entity)
			{
				bHadOld = GConfig->GetString(Section, *Key, OldTemplate, GEditorIni);
				GConfig->SetString(Section, *Key, *(FString(AgentTestRoot) / TEXT("{Folder}/{Name}")), GEditorIni);
			}

			void Restore()
			{
				if (bHadOld)
				{
					GConfig->SetString(Section, *Key, *OldTemplate, GEditorIni);
				}
				else
				{
					GConfig->RemoveKey(Section, *Key, GEditorIni);
				}
				IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
				TArray<FAssetData> Assets;
				Registry.GetAssetsByPath(FName(AgentTestRoot), Assets, true);
				TArray<UObject*> Objects;
				for (const FAssetData& Asset : Assets)
				{
					if (UObject* Object = Asset.GetAsset())
					{
						Objects.Add(Object);
					}
				}
				if (!Objects.IsEmpty())
				{
					ObjectTools::ForceDeleteObjects(Objects, false);
				}
				Registry.RemovePath(AgentTestRoot);
				const FString OnDisk = FPaths::ProjectContentDir() / FString(AgentTestRoot).RightChop(6);
				if (IFileManager::Get().DirectoryExists(*OnDisk))
				{
					IFileManager::Get().DeleteDirectory(*OnDisk, false, true);
				}
			}

		private:
			const TCHAR* const Section = TEXT("CoopKit.Workspace");
			FString Key;
			FString OldTemplate;
			bool bHadOld = false;
		};

		/**
		 * Sets bValidateOnSave of the editor's data validation settings (the DataValidation plugin, reached by reflection: the
		 * tests do not depend on the plugin) and returns the old value; true when the settings are not loaded.
		 */
		bool SetValidateOnSave(const bool bValidate)
		{
			const UClass* Settings = FindObject<UClass>(nullptr, TEXT("/Script/DataValidation.DataValidationSettings"));
			const FBoolProperty* Property = Settings != nullptr ? FindFProperty<FBoolProperty>(Settings, TEXT("bValidateOnSave")) : nullptr;
			if (Property == nullptr)
			{
				return true;
			}
			UObject* Defaults = Settings->GetDefaultObject();
			const bool bOld = Property->GetPropertyValue_InContainer(Defaults);
			Property->SetPropertyValue_InContainer(Defaults, bValidate);
			return bOld;
		}

		/** A new value for a simple field, or empty when the field's type is not one the test knows how to change. */
		FString ChangedValue(const FString& Type, const FString& Value)
		{
			if (Type == TEXT("bool"))
			{
				return Value == TEXT("True") ? TEXT("False") : TEXT("True");
			}
			if (Type == TEXT("int32"))
			{
				return FString::FromInt(FCString::Atoi(*Value) + 1);
			}
			if (Type == TEXT("float") || Type == TEXT("double"))
			{
				return FString::SanitizeFloat(FCString::Atod(*Value) + 1.5);
			}
			if (Type == TEXT("FString") || Type == TEXT("FName"))
			{
				return Value + TEXT("Agent");
			}
			return FString();
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCKSessionsAgentSurfaceTest, "CoopKit.Sessions.Agent.Surface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCKSessionsAgentSurfaceTest::RunTest(const FString& Parameters)
{
	using namespace CKSessions::Tests::Agent;

	// The envelope: schema, plugin, operation, ok, result, error (decision-129 of the CoopKit line).
	const TSharedPtr<FJsonObject> SurfaceResponse = Parse(UCKSessionsAgentLibrary::DescribeSurface());
	if (!TestTrue(TEXT("DescribeSurface returns a JSON object"), SurfaceResponse.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Schema version"), SurfaceResponse->GetStringField(TEXT("schema")), FString(CKSessions::Agent::SchemaVersion));
	TestEqual(TEXT("Plugin"), SurfaceResponse->GetStringField(TEXT("plugin")), FString(TEXT("CoopKitSessions")));
	TestEqual(TEXT("Operation"), SurfaceResponse->GetStringField(TEXT("operation")), FString(TEXT("DescribeSurface")));
	TestTrue(TEXT("A success has a null error"), SurfaceResponse->HasTypedField<EJson::Null>(TEXT("error")));
	const TSharedPtr<FJsonObject> Surface = ResultOf(SurfaceResponse);
	if (TestNotNull(TEXT("DescribeSurface succeeds"), Surface.Get()))
	{
		TestEqual(TEXT("Nine operations"), Surface->GetArrayField(TEXT("operations")).Num(), 9);
	}

	// The types: the same as the hub's, only this plugin's.
	const TSharedPtr<FJsonObject> Types = ResultOf(Parse(UCKSessionsAgentLibrary::ListEntityTypes()));
	if (!TestNotNull(TEXT("ListEntityTypes succeeds"), Types.Get()))
	{
		return false;
	}
	const TArray<CKSessions::Hub::FEntityType> HubTypes = CKSessions::Hub::CollectEntityTypes();
	TSharedPtr<FJsonObject> Creatable;
	for (const TSharedPtr<FJsonValue>& Value : Types->GetArrayField(TEXT("types")))
	{
		const TSharedPtr<FJsonObject> Type = Value->AsObject();
		const FString Entity = Type->GetStringField(TEXT("type"));
		TestTrue(FString::Printf(TEXT("The hub lists the type %s too"), *Entity), HubTypes.ContainsByPredicate([&Entity](const CKSessions::Hub::FEntityType& HubType) { return HubType.Entity == Entity; }));
		TestTrue(FString::Printf(TEXT("%s is a type of this plugin"), *Entity), Type->GetStringField(TEXT("module")).StartsWith(TEXT("CoopKitSessions")));
		if (!Creatable.IsValid() && Type->GetBoolField(TEXT("canCreate")))
		{
			Creatable = Type;
		}
	}
	TestTrue(TEXT("The plugin has entity types"), Types->GetArrayField(TEXT("types")).Num() > 0);

	// Errors: a code for programs, nothing changed.
	TestEqual(TEXT("Unknown type: ListEntities"), ErrorCodeOf(UCKSessionsAgentLibrary::ListEntities(TEXT("NoSuchAgentType"))), FString(TEXT("unknown_type")));
	TestEqual(TEXT("Unknown type: CreateEntity"), ErrorCodeOf(UCKSessionsAgentLibrary::CreateEntity(TEXT("NoSuchAgentType"), TEXT("Alpha"))), FString(TEXT("unknown_type")));
	TestEqual(TEXT("Missing asset"), ErrorCodeOf(UCKSessionsAgentLibrary::DescribeEntity(FString(AgentTestRoot) / TEXT("Missing/DA_Missing"))), FString(TEXT("asset_not_found")));
	TestEqual(TEXT("Empty path"), ErrorCodeOf(UCKSessionsAgentLibrary::ValidateEntity(FString())), FString(TEXT("asset_not_found")));
	TestEqual(TEXT("An asset that is not an entity"), ErrorCodeOf(UCKSessionsAgentLibrary::ExportEntity(TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"))), FString(TEXT("not_an_entity")));
	const TSharedPtr<FJsonObject> Failed = Parse(UCKSessionsAgentLibrary::ListEntities(TEXT("NoSuchAgentType")));
	TestTrue(TEXT("A failure has a null result and a message"), Failed.IsValid() && Failed->HasTypedField<EJson::Null>(TEXT("result"))
		&& !Failed->GetObjectField(TEXT("error"))->GetStringField(TEXT("message")).IsEmpty());

	if (!TestNotNull(TEXT("A type the agent can create"), Creatable.Get()))
	{
		return false;
	}
	const FString TypeName = Creatable->GetStringField(TEXT("type"));
	const CKSessions::Hub::FEntityType* HubType = HubTypes.FindByPredicate([&TypeName](const CKSessions::Hub::FEntityType& Type) { return Type.Entity == TypeName; });
	if (!TestNotNull(TEXT("The hub type of the created entity"), HubType))
	{
		return false;
	}
	const TSharedRef<FAgentWorkspace> Workspace = MakeShared<FAgentWorkspace>(TypeName);
	const double CleanupAt = FPlatformTime::Seconds() + 3.0;
	AddCommand(new FFunctionLatentCommand([Workspace, CleanupAt]()
	{
		if (FPlatformTime::Seconds() < CleanupAt)
		{
			return false;
		}
		Workspace->Restore();
		return true;
	}));
	TestEqual(TEXT("Invalid name"), ErrorCodeOf(UCKSessionsAgentLibrary::CreateEntity(TypeName, TEXT("Bad Name"))), FString(TEXT("invalid_name")));

	// Create: where the New wizard of the hub puts it, listed by the hub.
	const FString Name = TEXT("Agent") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
	const TSharedPtr<FJsonObject> Created = ResultOf(Parse(UCKSessionsAgentLibrary::CreateEntity(Creatable->GetStringField(TEXT("class")), Name)));
	if (!TestNotNull(TEXT("CreateEntity by class name succeeds"), Created.Get()))
	{
		return false;
	}
	const FString AssetName = CKSessions::Hub::MakeEntityAssetName(*HubType, Name);
	const FString Path = CKSessions::Hub::ResolveEntityFolder(*HubType, Name) / AssetName + TEXT(".") + AssetName;
	TestEqual(TEXT("The path the hub's New gives"), Created->GetStringField(TEXT("path")), Path);
	TestTrue(TEXT("The path is in the test folder"), Path.StartsWith(AgentTestRoot));
	TestEqual(TEXT("Creating it again"), ErrorCodeOf(UCKSessionsAgentLibrary::CreateEntity(TypeName, Name)), FString(TEXT("already_exists")));
	UObject* Entity = FindObject<UObject>(nullptr, *Path);
	if (!TestNotNull(TEXT("The entity exists"), Entity))
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Listed = ResultOf(Parse(UCKSessionsAgentLibrary::ListEntities(TypeName)));
	TestTrue(TEXT("ListEntities lists the new entity"), Listed.IsValid() && Listed->GetArrayField(TEXT("entities")).ContainsByPredicate([&Path](const TSharedPtr<FJsonValue>& Value)
	{
		return Value->AsObject()->GetStringField(TEXT("path")) == Path;
	}));

	// Validate: what the Validation tab of the card shows.
	const CKSessions::Editor::FValidationReport Report = CKSessions::Editor::ValidateData(*Entity);
	const TSharedPtr<FJsonObject> Validation = ResultOf(Parse(UCKSessionsAgentLibrary::ValidateEntity(Path)));
	if (TestNotNull(TEXT("ValidateEntity succeeds"), Validation.Get()))
	{
		TestEqual(TEXT("Valid as the card says"), Validation->GetBoolField(TEXT("valid")), Report.Result != EDataValidationResult::Invalid);
		TestEqual(TEXT("The card's errors"), Validation->GetArrayField(TEXT("errors")).Num(), Report.Errors.Num());
		TestEqual(TEXT("The card's warnings"), Validation->GetArrayField(TEXT("warnings")).Num(), Report.Warnings.Num());
	}

	// Describe and export agree; an export imported back changes nothing.
	const TSharedPtr<FJsonObject> Described = ResultOf(Parse(UCKSessionsAgentLibrary::DescribeEntity(Path)));
	const FString ExportText = UCKSessionsAgentLibrary::ExportEntity(Path);
	const TSharedPtr<FJsonObject> Exported = ResultOf(Parse(ExportText));
	if (!TestNotNull(TEXT("DescribeEntity succeeds"), Described.Get()) || !TestNotNull(TEXT("ExportEntity succeeds"), Exported.Get()))
	{
		return false;
	}
	TestEqual(TEXT("Describe names the type"), Described->GetStringField(TEXT("type")), TypeName);
	TestEqual(TEXT("Describe names the entity"), Described->GetStringField(TEXT("name")), Name);
	const TSharedPtr<FJsonObject> Values = Exported->GetObjectField(TEXT("fields"));
	const TArray<TSharedPtr<FJsonValue>>& Fields = Described->GetArrayField(TEXT("fields"));
	TestTrue(TEXT("The entity has editable fields"), Fields.Num() > 0);
	TestEqual(TEXT("Describe and export list the same fields"), Fields.Num(), Values->Values.Num());
	TSharedPtr<FJsonObject> Simple;
	TSharedPtr<FJsonObject> Boolean;
	for (const TSharedPtr<FJsonValue>& Value : Fields)
	{
		const TSharedPtr<FJsonObject> Field = Value->AsObject();
		const FString Key = Field->GetStringField(TEXT("name"));
		FString Exported1;
		TestTrue(FString::Printf(TEXT("Export has %s with the described value"), *Key), Values->TryGetStringField(Key, Exported1) && Exported1 == Field->GetStringField(TEXT("value")));
		if (!Field->GetBoolField(TEXT("readOnly")))
		{
			if (!Simple.IsValid() && !ChangedValue(Field->GetStringField(TEXT("type")), Field->GetStringField(TEXT("value"))).IsEmpty())
			{
				Simple = Field;
			}
			if (!Boolean.IsValid() && Field->GetStringField(TEXT("type")) == TEXT("bool"))
			{
				Boolean = Field;
			}
		}
	}
	const FString RoundTripText = UCKSessionsAgentLibrary::ImportEntity(Path, ExportText);
	const TSharedPtr<FJsonObject> RoundTrip = ResultOf(Parse(RoundTripText));
	if (!RoundTrip.IsValid())
	{
		AddError(FString::Printf(TEXT("ImportEntity of the export: %s"), *RoundTripText));
	}
	else
	{
		TestEqual(TEXT("Importing the export changes nothing"), RoundTrip->GetArrayField(TEXT("changed")).Num(), 0);
		TestTrue(TEXT("Import reports the validation"), RoundTrip->HasTypedField<EJson::Object>(TEXT("validation")));
	}
	const FString Before = ToText(ResultOf(Parse(UCKSessionsAgentLibrary::ExportEntity(Path)))->GetObjectField(TEXT("fields")));
	TestEqual(TEXT("The export is the same after the round trip"), Before, ToText(Values));

	// All or nothing: an unknown field, bad JSON or a bad value change nothing.
	TestEqual(TEXT("Unknown field"), ErrorCodeOf(UCKSessionsAgentLibrary::ImportEntity(Path, TEXT("{\"NoSuchAgentField\": \"1\"}"))), FString(TEXT("unknown_field")));
	TestEqual(TEXT("Not JSON"), ErrorCodeOf(UCKSessionsAgentLibrary::ImportEntity(Path, TEXT("{"))), FString(TEXT("invalid_json")));
	if (Boolean.IsValid())
	{
		const FString Bad = FString::Printf(TEXT("{\"%s\": \"maybe\"}"), *Boolean->GetStringField(TEXT("name")));
		TestEqual(TEXT("A value the field cannot take"), ErrorCodeOf(UCKSessionsAgentLibrary::ImportEntity(Path, Bad)), FString(TEXT("invalid_value")));
	}
	TestEqual(TEXT("Failed imports changed nothing"), ToText(ResultOf(Parse(UCKSessionsAgentLibrary::ExportEntity(Path)))->GetObjectField(TEXT("fields"))), Before);

	// A real change: the field takes the new value, the export and the Details panel (reflection) show it.
	if (Simple.IsValid())
	{
		const FString Key = Simple->GetStringField(TEXT("name"));
		const FString NewValue = ChangedValue(Simple->GetStringField(TEXT("type")), Simple->GetStringField(TEXT("value")));
		const TSharedRef<FJsonObject> Change = MakeShared<FJsonObject>();
		Change->SetStringField(Key, NewValue);
		const TSharedPtr<FJsonObject> Imported = ResultOf(Parse(UCKSessionsAgentLibrary::ImportEntity(Path, ToText(Change))));
		if (TestNotNull(FString::Printf(TEXT("Import of %s = %s succeeds"), *Key, *NewValue), Imported.Get()))
		{
			TestEqual(TEXT("Import names the changed field"), Imported->GetArrayField(TEXT("changed")).Num(), 1);
			FString After;
			ResultOf(Parse(UCKSessionsAgentLibrary::ExportEntity(Path)))->GetObjectField(TEXT("fields"))->TryGetStringField(Key, After);
			TestNotEqual(TEXT("The export shows the new value"), After, Simple->GetStringField(TEXT("value")));
			const FProperty* Property = Entity->GetClass()->FindPropertyByName(FName(*Key));
			FString Direct;
			if (TestNotNull(TEXT("The field is a property"), Property))
			{
				Property->ExportTextItem_Direct(Direct, Property->ContainerPtrToValuePtr<void>(Entity), nullptr, Entity, PPF_None);
				TestEqual(TEXT("The property holds what the export says"), Direct, After);
			}
		}
	}
	else
	{
		AddInfo(FString::Printf(TEXT("%s has no bool, int32, float, double, FString or FName field: the change step is skipped."), *TypeName));
	}

	// Save: the file reaches the disk; the project check sees the plugin's entities. A new entity keeps its defaults, which
	// its own validation may warn about; the editor's validation on save would log those warnings into this test.
	const FString SavedName = Name + TEXT("Saved");
	const bool bValidatedOnSave = SetValidateOnSave(false);
	const TSharedPtr<FJsonObject> Saved = ResultOf(Parse(UCKSessionsAgentLibrary::CreateEntity(TypeName, SavedName, true)));
	SetValidateOnSave(bValidatedOnSave);
	if (TestNotNull(TEXT("CreateEntity with save succeeds"), Saved.Get()))
	{
		TestTrue(TEXT("The response says saved"), Saved->GetBoolField(TEXT("saved")));
		TestTrue(TEXT("The package is on disk"), FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Saved->GetStringField(TEXT("path")))));
	}
	const TSharedPtr<FJsonObject> Project = ResultOf(Parse(UCKSessionsAgentLibrary::CheckProject()));
	if (TestNotNull(TEXT("CheckProject succeeds"), Project.Get()))
	{
		TestTrue(TEXT("The project check counts the new entities"), Project->GetNumberField(TEXT("entities")) >= 2.0);
		TestTrue(TEXT("The project check lists issues"), Project->HasTypedField<EJson::Array>(TEXT("issues")));
	}
	AddInfo(FString::Printf(TEXT("Agent surface checked on %s (%d types, %d fields)."), *TypeName, Types->GetArrayField(TEXT("types")).Num(), Fields.Num()));
	return true;
}

#endif
