// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.
// CoopKit shared file: the same agent surface ships in every CoopKit plugin with a hub, under the plugin's prefix. CoopKit shared group: hub.

#include "CKSessionsAgentLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Factories/Factory.h"
#include "FileHelpers.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/PackageName.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "CKSessionsDataValidation.h"
#include "CKSessionsHub.h"

#define LOCTEXT_NAMESPACE "CKSessionsAgentLibrary"

namespace CKSessions::Agent
{
	namespace
	{
		const TCHAR* const PluginName = TEXT("CoopKitSessions");
		const TCHAR* const PluginPrefix = TEXT("CKSessions");
		const FName PartKey(TEXT("CoopKitPart"));

		/** Operation names: DescribeSurface lists them, every response names its own. */
		const TCHAR* const OpDescribeSurface = TEXT("DescribeSurface");
		const TCHAR* const OpListEntityTypes = TEXT("ListEntityTypes");
		const TCHAR* const OpListEntities = TEXT("ListEntities");
		const TCHAR* const OpCreateEntity = TEXT("CreateEntity");
		const TCHAR* const OpDescribeEntity = TEXT("DescribeEntity");
		const TCHAR* const OpValidateEntity = TEXT("ValidateEntity");
		const TCHAR* const OpCheckProject = TEXT("CheckProject");
		const TCHAR* const OpExportEntity = TEXT("ExportEntity");
		const TCHAR* const OpImportEntity = TEXT("ImportEntity");

		/** Why an operation failed: a stable code for programs and a message for people. */
		struct FFailure
		{
			FString Code;
			FString Message;
		};

		/** One editable field of an entity: a property, or one element of a fixed-size C++ array property. */
		struct FEntityField
		{
			FProperty* Property = nullptr;
			int32 Index = 0;
			FString Key;
			bool bReadOnly = false;
		};

		/** Collects what ImportText reports, so a bad value becomes an error code instead of a log line. */
		class FCaptureOutput final : public FOutputDevice
		{
		public:
			FString Text;

			virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
			{
				if (!Text.IsEmpty())
				{
					Text += TEXT(" ");
				}
				Text += Message;
			}
		};

		FString ToJsonText(const TSharedRef<FJsonObject>& Object)
		{
			FString Out;
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
			FJsonSerializer::Serialize(Object, Writer);
			return Out;
		}

		FString Respond(const TCHAR* Operation, const TSharedPtr<FJsonObject>& Result, const FFailure* Failure = nullptr)
		{
			const TSharedRef<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("schema"), SchemaVersion);
			Response->SetStringField(TEXT("plugin"), PluginName);
			Response->SetStringField(TEXT("operation"), Operation);
			Response->SetBoolField(TEXT("ok"), Failure == nullptr);
			if (Failure == nullptr && Result.IsValid())
			{
				Response->SetObjectField(TEXT("result"), Result);
			}
			else
			{
				Response->SetField(TEXT("result"), MakeShared<FJsonValueNull>());
			}
			if (Failure != nullptr)
			{
				const TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
				Error->SetStringField(TEXT("code"), Failure->Code);
				Error->SetStringField(TEXT("message"), Failure->Message);
				Response->SetObjectField(TEXT("error"), Error);
			}
			else
			{
				Response->SetField(TEXT("error"), MakeShared<FJsonValueNull>());
			}
			return ToJsonText(Response);
		}

		FString Fail(const TCHAR* Operation, const FFailure& Failure)
		{
			return Respond(Operation, nullptr, &Failure);
		}

		/** Modules of this plugin: its entity types are the ones declared in them. */
		TSet<FString> OwnModules()
		{
			TSet<FString> Modules;
			Modules.Add(PluginName);
			if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName))
			{
				for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
				{
					Modules.Add(Module.Name.ToString());
				}
			}
			return Modules;
		}

		/** Entity types of this plugin; the hub also lists the types of other CoopKit plugins, their libraries serve those. */
		TArray<Hub::FEntityType> OwnTypes()
		{
			const TSet<FString> Modules = OwnModules();
			TArray<Hub::FEntityType> Types = Hub::CollectEntityTypes();
			Types.RemoveAll([&Modules](const Hub::FEntityType& Type) { return !Modules.Contains(Type.Plugin); });
			return Types;
		}

		bool FindOwnType(const FString& TypeName, Hub::FEntityType& OutType)
		{
			const FString Wanted = TypeName.TrimStartAndEnd();
			for (const Hub::FEntityType& Type : OwnTypes())
			{
				const FString ClassName = Type.Class->GetName();
				if (Wanted.Equals(Type.Entity, ESearchCase::IgnoreCase) || Wanted.Equals(Type.Entity.Replace(TEXT(" "), TEXT("")), ESearchCase::IgnoreCase)
					|| Wanted.Equals(ClassName, ESearchCase::IgnoreCase) || Wanted.Equals(Type.Class->GetPrefixCPP() + ClassName, ESearchCase::IgnoreCase))
				{
					OutType = Type;
					return true;
				}
			}
			return false;
		}

		FFailure UnknownType(const FString& TypeName)
		{
			TArray<FString> Names;
			for (const Hub::FEntityType& Type : OwnTypes())
			{
				Names.Add(Type.Entity);
			}
			return { TEXT("unknown_type"), FString::Printf(TEXT("%s has no entity type \"%s\". Types: %s."), PluginName, *TypeName, *FString::Join(Names, TEXT(", "))) };
		}

		/** The entity's name in asset names: the asset name without the DA_<Entity>_ prefix of the hub. */
		FString EntityNameOf(const Hub::FEntityType& Type, const FString& AssetName)
		{
			const FString Prefix = FString::Printf(TEXT("DA_%s_"), *Type.Entity.Replace(TEXT(" "), TEXT("")));
			return AssetName.StartsWith(Prefix) ? AssetName.RightChop(Prefix.Len()) : AssetName;
		}

		/** Object path of an asset given by its object path or its package path. */
		FString ToObjectPath(const FString& AssetPath)
		{
			FString Path = AssetPath.TrimStartAndEnd();
			if (!Path.IsEmpty() && !Path.Contains(TEXT(".")))
			{
				Path += TEXT(".") + FPackageName::GetShortName(Path);
			}
			return Path;
		}

		/** Loads the entity and finds its type among this plugin's types (the class or its nearest native parent with the metadata). */
		UObject* ResolveEntity(const FString& AssetPath, Hub::FEntityType& OutType, FFailure& OutFailure)
		{
			// An unsaved entity lives only in memory; a saved one may need loading. A path with no package on disk is not
			// loaded at all, so a wrong path is an error code and not a warning in the log.
			const FString ObjectPath = ToObjectPath(AssetPath);
			UObject* Entity = nullptr;
			if (!ObjectPath.IsEmpty() && FPackageName::IsValidObjectPath(ObjectPath))
			{
				const FSoftObjectPath Path(ObjectPath);
				Entity = Path.ResolveObject();
				if (Entity == nullptr && FPackageName::DoesPackageExist(Path.GetLongPackageName()))
				{
					Entity = Path.TryLoad();
				}
			}
			if (!IsValid(Entity))
			{
				OutFailure = { TEXT("asset_not_found"), FString::Printf(TEXT("No asset at \"%s\": give the object path (/Game/Folder/DA_Name.DA_Name) or the package path."), *AssetPath) };
				return nullptr;
			}
			const TSet<FString> Modules = OwnModules();
			for (const UClass* Class = Entity->GetClass(); Class != nullptr; Class = Class->GetSuperClass())
			{
				if (Hub::FindEntityType(Class, OutType))
				{
					if (Modules.Contains(OutType.Plugin))
					{
						return Entity;
					}
					OutFailure = { TEXT("not_an_entity"), FString::Printf(TEXT("%s is a %s entity of %s: use that plugin's agent library."), *AssetPath, *OutType.Entity, *OutType.Plugin) };
					return nullptr;
				}
			}
			OutFailure = { TEXT("not_an_entity"), FString::Printf(TEXT("%s (%s) is not an entity of %s."), *AssetPath, *Entity->GetClass()->GetName(), PluginName) };
			return nullptr;
		}

		/** Fields the Details panel lets people edit and the asset saves: EditAnywhere/EditDefaultsOnly, not transient or deprecated. */
		TArray<FEntityField> CollectFields(const UObject& Entity)
		{
			TArray<FEntityField> Fields;
			for (TFieldIterator<FProperty> It(Entity.GetClass()); It; ++It)
			{
				FProperty* Property = *It;
				if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated))
				{
					continue;
				}
				for (int32 Index = 0; Index < Property->ArrayDim; ++Index)
				{
					FEntityField Field;
					Field.Property = Property;
					Field.Index = Index;
					Field.Key = Property->ArrayDim > 1 ? FString::Printf(TEXT("%s[%d]"), *Property->GetName(), Index) : Property->GetName();
					Field.bReadOnly = Property->HasAnyPropertyFlags(CPF_EditConst);
					Fields.Add(Field);
				}
			}
			return Fields;
		}

		/** The field's value as text. ExportTextItem, not ExportText: ExportText skips a value equal to the zero default (False, 0). */
		FString ExportValue(const FEntityField& Field, UObject& Entity)
		{
			FString Value;
			Field.Property->ExportTextItem_Direct(Value, Field.Property->ContainerPtrToValuePtr<void>(&Entity, Field.Index), nullptr, &Entity, PPF_None);
			return Value;
		}

		/** Parses Text into Value (memory of one element of the field's property). False with OutError when the text does not fit. */
		bool ImportInto(const FEntityField& Field, UObject& Entity, void* Value, const FString& Text, FString& OutError)
		{
			FCaptureOutput Errors;
			const TCHAR* End = Field.Property->ImportText_Direct(*Text, Value, &Entity, PPF_None, &Errors);
			if (End != nullptr)
			{
				while (FChar::IsWhitespace(*End))
				{
					++End;
				}
			}
			if (End == nullptr || *End != TEXT('\0') || !Errors.Text.IsEmpty())
			{
				OutError = FString::Printf(TEXT("\"%s\" is not a value of %s (%s)%s%s"), *Text, *Field.Key, *Field.Property->GetCPPType(),
					Errors.Text.IsEmpty() ? TEXT("") : TEXT(": "), *Errors.Text);
				return false;
			}
			return true;
		}

		/** Text of a JSON value for ImportText: strings as they are, booleans as True/False, whole numbers without a fraction. */
		bool JsonValueToText(const TSharedPtr<FJsonValue>& Value, FString& OutText)
		{
			if (!Value.IsValid())
			{
				return false;
			}
			switch (Value->Type)
			{
			case EJson::String:
				OutText = Value->AsString();
				return true;
			case EJson::Boolean:
				OutText = Value->AsBool() ? TEXT("True") : TEXT("False");
				return true;
			case EJson::Number:
			{
				const double Number = Value->AsNumber();
				OutText = FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number), UE_DOUBLE_SMALL_NUMBER) && FMath::Abs(Number) < 9.0e15
					? FString::Printf(TEXT("%lld"), static_cast<int64>(FMath::RoundToDouble(Number)))
					: FString::SanitizeFloat(Number);
				return true;
			}
			default:
				return false;
			}
		}

		bool ContainsWord(const FString& Message, const FString& Word)
		{
			if (Word.IsEmpty())
			{
				return false;
			}
			int32 From = 0;
			while (true)
			{
				const int32 At = Message.Find(Word, ESearchCase::CaseSensitive, ESearchDir::FromStart, From);
				if (At == INDEX_NONE)
				{
					return false;
				}
				const int32 After = At + Word.Len();
				const bool bStart = At == 0 || !FChar::IsAlnum(Message[At - 1]);
				const bool bEnd = After >= Message.Len() || !FChar::IsAlnum(Message[After]);
				if (bStart && bEnd)
				{
					return true;
				}
				From = At + 1;
			}
		}

		/** The field a validation message names (its property name or its Details panel name), the longest match; empty when none. */
		FString FieldOfMessage(const FString& Message, const TArray<FEntityField>& Fields)
		{
			FString Best;
			int32 BestLen = 0;
			for (const FEntityField& Field : Fields)
			{
				TArray<FString> Names = { Field.Property->GetName() };
#if WITH_EDITORONLY_DATA
				Names.Add(Field.Property->GetDisplayNameText().ToString());
#endif
				for (const FString& Name : Names)
				{
					if (Name.Len() > BestLen && ContainsWord(Message, Name))
					{
						Best = Field.Key;
						BestLen = Name.Len();
					}
				}
			}
			return Best;
		}

		TArray<TSharedPtr<FJsonValue>> IssuesJson(const TArray<FText>& Messages, const TArray<FEntityField>& Fields)
		{
			TArray<TSharedPtr<FJsonValue>> Out;
			for (const FText& Message : Messages)
			{
				const TSharedRef<FJsonObject> Issue = MakeShared<FJsonObject>();
				Issue->SetStringField(TEXT("field"), FieldOfMessage(Message.ToString(), Fields));
				Issue->SetStringField(TEXT("message"), Message.ToString());
				Out.Add(MakeShared<FJsonValueObject>(Issue));
			}
			return Out;
		}

		TSharedRef<FJsonObject> ValidationJson(const UObject& Entity)
		{
			const CKSessions::Editor::FValidationReport Report = CKSessions::Editor::ValidateData(Entity);
			const TArray<FEntityField> Fields = CollectFields(Entity);
			const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("path"), Entity.GetPathName());
			Result->SetBoolField(TEXT("valid"), Report.Result != EDataValidationResult::Invalid);
			Result->SetStringField(TEXT("result"), Report.Result == EDataValidationResult::Valid ? TEXT("Valid")
				: Report.Result == EDataValidationResult::Invalid ? TEXT("Invalid") : TEXT("NotValidated"));
			Result->SetArrayField(TEXT("errors"), IssuesJson(Report.Errors, Fields));
			Result->SetArrayField(TEXT("warnings"), IssuesJson(Report.Warnings, Fields));
			return Result;
		}

		bool SaveAsset(UObject& Entity)
		{
			return UEditorLoadingAndSavingUtils::SavePackages({ Entity.GetOutermost() }, false);
		}

		TSharedRef<FJsonObject> FieldsJson(UObject& Entity)
		{
			const TSharedRef<FJsonObject> Values = MakeShared<FJsonObject>();
			for (const FEntityField& Field : CollectFields(Entity))
			{
				Values->SetStringField(Field.Key, ExportValue(Field, Entity));
			}
			return Values;
		}
	}
}

FString UCKSessionsAgentLibrary::DescribeSurface()
{
	using namespace CKSessions::Agent;
	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("plugin"), PluginName);
	Result->SetStringField(TEXT("prefix"), PluginPrefix);
	Result->SetStringField(TEXT("library"), FString::Printf(TEXT("%sAgentLibrary"), PluginPrefix));
	Result->SetStringField(TEXT("schema"), SchemaVersion);
	TArray<TSharedPtr<FJsonValue>> Operations;
	for (const TCHAR* Operation : { OpDescribeSurface, OpListEntityTypes, OpListEntities, OpCreateEntity, OpDescribeEntity, OpValidateEntity, OpCheckProject, OpExportEntity, OpImportEntity })
	{
		Operations.Add(MakeShared<FJsonValueString>(Operation));
	}
	Result->SetArrayField(TEXT("operations"), Operations);
	Result->SetNumberField(TEXT("entityTypes"), OwnTypes().Num());
	return Respond(OpDescribeSurface, Result);
}

FString UCKSessionsAgentLibrary::ListEntityTypes()
{
	using namespace CKSessions::Agent;
	TArray<TSharedPtr<FJsonValue>> Types;
	for (const CKSessions::Hub::FEntityType& Type : OwnTypes())
	{
		const UFactory* Factory = CKSessions::Hub::FindCreateFactory(Type.Class);
		const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("type"), Type.Entity);
		Item->SetStringField(TEXT("class"), Type.Class->GetPrefixCPP() + Type.Class->GetName());
		Item->SetStringField(TEXT("classPath"), Type.Class->GetPathName());
		Item->SetStringField(TEXT("module"), Type.Plugin);
		Item->SetStringField(TEXT("folder"), Type.Folder);
		Item->SetStringField(TEXT("idField"), Type.IdProperty.IsNone() ? FString() : Type.IdProperty.ToString());
		Item->SetStringField(TEXT("factory"), Factory != nullptr ? Factory->GetClass()->GetName() : FString());
		Item->SetBoolField(TEXT("canCreate"), Factory != nullptr);
		Item->SetNumberField(TEXT("entities"), CKSessions::Hub::FindEntities(Type).Num());
		Types.Add(MakeShared<FJsonValueObject>(Item));
	}
	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("types"), Types);
	return Respond(OpListEntityTypes, Result);
}

FString UCKSessionsAgentLibrary::ListEntities(const FString& TypeName)
{
	using namespace CKSessions::Agent;
	CKSessions::Hub::FEntityType Type;
	if (!FindOwnType(TypeName, Type))
	{
		return Fail(OpListEntities, UnknownType(TypeName));
	}
	TArray<TSharedPtr<FJsonValue>> Entities;
	for (const FAssetData& Asset : CKSessions::Hub::FindEntities(Type))
	{
		const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("path"), Asset.GetSoftObjectPath().ToString());
		Item->SetStringField(TEXT("asset"), Asset.AssetName.ToString());
		Item->SetStringField(TEXT("name"), EntityNameOf(Type, Asset.AssetName.ToString()));
		Item->SetStringField(TEXT("displayName"), CKSessions::Hub::GetEntityDisplayName(Asset).ToString());
		Entities.Add(MakeShared<FJsonValueObject>(Item));
	}
	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("type"), Type.Entity);
	Result->SetArrayField(TEXT("entities"), Entities);
	return Respond(OpListEntities, Result);
}

FString UCKSessionsAgentLibrary::CreateEntity(const FString& TypeName, const FString& Name, const bool bSave)
{
	using namespace CKSessions::Agent;
	CKSessions::Hub::FEntityType Type;
	if (!FindOwnType(TypeName, Type))
	{
		return Fail(OpCreateEntity, UnknownType(TypeName));
	}
	if (!CKSessions::Hub::IsValidEntityName(Name))
	{
		return Fail(OpCreateEntity, { TEXT("invalid_name"), FString::Printf(TEXT("\"%s\": letters, digits and underscores, starting with a letter, at most 64 characters."), *Name) });
	}
	if (CKSessions::Hub::FindCreateFactory(Type.Class) == nullptr)
	{
		return Fail(OpCreateEntity, { TEXT("no_factory"), FString::Printf(TEXT("%s has no factory: it is not created from the Content Browser."), *Type.Entity) });
	}
	const FString AssetName = CKSessions::Hub::MakeEntityAssetName(Type, Name);
	const FString PackageName = CKSessions::Hub::ResolveEntityFolder(Type, Name) / AssetName;
	const FString ObjectPath = PackageName + TEXT(".") + AssetName;
	if (IsValid(FindObject<UObject>(nullptr, *ObjectPath)) || FPackageName::DoesPackageExist(PackageName))
	{
		return Fail(OpCreateEntity, { TEXT("already_exists"), FString::Printf(TEXT("%s already exists."), *ObjectPath) });
	}
	UObject* Entity = CKSessions::Hub::CreateEntity(Type, Name, nullptr, true);
	if (Entity == nullptr)
	{
		return Fail(OpCreateEntity, { TEXT("create_failed"), FString::Printf(TEXT("The factory of %s created nothing at %s (see the Output Log)."), *Type.Entity, *ObjectPath) });
	}
	if (bSave && !SaveAsset(*Entity))
	{
		return Fail(OpCreateEntity, { TEXT("save_failed"), FString::Printf(TEXT("%s was created but not saved (see the Output Log)."), *Entity->GetPathName()) });
	}
	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("path"), Entity->GetPathName());
	Result->SetStringField(TEXT("asset"), Entity->GetName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("type"), Type.Entity);
	Result->SetStringField(TEXT("folder"), FPackageName::GetLongPackagePath(Entity->GetOutermost()->GetName()));
	Result->SetBoolField(TEXT("saved"), bSave);
	return Respond(OpCreateEntity, Result);
}

FString UCKSessionsAgentLibrary::DescribeEntity(const FString& AssetPath)
{
	using namespace CKSessions::Agent;
	CKSessions::Hub::FEntityType Type;
	FFailure Failure;
	UObject* Entity = ResolveEntity(AssetPath, Type, Failure);
	if (Entity == nullptr)
	{
		return Fail(OpDescribeEntity, Failure);
	}
	TArray<TSharedPtr<FJsonValue>> Fields;
	for (const FEntityField& Field : CollectFields(*Entity))
	{
		const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("name"), Field.Key);
		Item->SetStringField(TEXT("type"), Field.Property->GetCPPType());
#if WITH_METADATA
		Item->SetStringField(TEXT("category"), Field.Property->GetMetaData(TEXT("Category")));
		Item->SetStringField(TEXT("tooltip"), Field.Property->GetMetaData(TEXT("ToolTip")));
#endif
		Item->SetStringField(TEXT("value"), ExportValue(Field, *Entity));
		Item->SetBoolField(TEXT("readOnly"), Field.bReadOnly);
		Fields.Add(MakeShared<FJsonValueObject>(Item));
	}

	// Parts: soft object references to other assets marked CoopKitPart (the Parts tab of the card, decision-63).
	TArray<TSharedPtr<FJsonValue>> Parts;
#if WITH_METADATA
	const FString Folder = FPackageName::GetLongPackagePath(Entity->GetOutermost()->GetName());
	for (TFieldIterator<FSoftObjectProperty> It(Entity->GetClass()); It; ++It)
	{
		const FSoftObjectProperty* Property = *It;
		if (Property->IsA<FSoftClassProperty>() || !Property->HasMetaData(PartKey))
		{
			continue;
		}
		const FSoftObjectPath Asset = Property->GetPropertyValue_InContainer(Entity).ToSoftObjectPath();
		const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("field"), Property->GetName());
		Item->SetStringField(TEXT("label"), Property->GetMetaData(PartKey));
		Item->SetStringField(TEXT("asset"), Asset.ToString());
		Item->SetStringField(TEXT("class"), Property->PropertyClass != nullptr ? Property->PropertyClass->GetName() : FString());
		Item->SetBoolField(TEXT("own"), !Asset.IsNull() && Asset.GetLongPackageName().StartsWith(Folder + TEXT("/")));
		Parts.Add(MakeShared<FJsonValueObject>(Item));
	}
#endif

	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("path"), Entity->GetPathName());
	Result->SetStringField(TEXT("asset"), Entity->GetName());
	Result->SetStringField(TEXT("name"), EntityNameOf(Type, Entity->GetName()));
	Result->SetStringField(TEXT("displayName"), CKSessions::Hub::GetEntityDisplayName(FAssetData(Entity)).ToString());
	Result->SetStringField(TEXT("type"), Type.Entity);
	Result->SetStringField(TEXT("class"), Entity->GetClass()->GetPrefixCPP() + Entity->GetClass()->GetName());
	Result->SetStringField(TEXT("folder"), FPackageName::GetLongPackagePath(Entity->GetOutermost()->GetName()));
	Result->SetBoolField(TEXT("dirty"), Entity->GetOutermost()->IsDirty());
	Result->SetArrayField(TEXT("fields"), Fields);
	Result->SetArrayField(TEXT("parts"), Parts);
	return Respond(OpDescribeEntity, Result);
}

FString UCKSessionsAgentLibrary::ValidateEntity(const FString& AssetPath)
{
	using namespace CKSessions::Agent;
	CKSessions::Hub::FEntityType Type;
	FFailure Failure;
	const UObject* Entity = ResolveEntity(AssetPath, Type, Failure);
	if (Entity == nullptr)
	{
		return Fail(OpValidateEntity, Failure);
	}
	return Respond(OpValidateEntity, ValidationJson(*Entity));
}

FString UCKSessionsAgentLibrary::CheckProject()
{
	using namespace CKSessions::Agent;
	TSet<FSoftObjectPath> OwnEntities;
	for (const CKSessions::Hub::FEntityType& Type : OwnTypes())
	{
		for (const FAssetData& Asset : CKSessions::Hub::FindEntities(Type))
		{
			OwnEntities.Add(Asset.GetSoftObjectPath());
		}
	}
	TArray<TSharedPtr<FJsonValue>> Issues;
	int32 Errors = 0;
	for (const CKSessions::Hub::FProjectIssue& Issue : CKSessions::Hub::CheckProject())
	{
		if (!OwnEntities.Contains(Issue.Asset))
		{
			continue;
		}
		const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("path"), Issue.Asset.ToString());
		Item->SetStringField(TEXT("severity"), Issue.bError ? TEXT("error") : TEXT("warning"));
		Item->SetStringField(TEXT("message"), Issue.Message.ToString());
		Issues.Add(MakeShared<FJsonValueObject>(Item));
		Errors += Issue.bError ? 1 : 0;
	}
	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("entities"), OwnEntities.Num());
	Result->SetNumberField(TEXT("errors"), Errors);
	Result->SetNumberField(TEXT("warnings"), Issues.Num() - Errors);
	Result->SetArrayField(TEXT("issues"), Issues);
	return Respond(OpCheckProject, Result);
}

FString UCKSessionsAgentLibrary::ExportEntity(const FString& AssetPath)
{
	using namespace CKSessions::Agent;
	CKSessions::Hub::FEntityType Type;
	FFailure Failure;
	UObject* Entity = ResolveEntity(AssetPath, Type, Failure);
	if (Entity == nullptr)
	{
		return Fail(OpExportEntity, Failure);
	}
	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("path"), Entity->GetPathName());
	Result->SetStringField(TEXT("type"), Type.Entity);
	Result->SetObjectField(TEXT("fields"), FieldsJson(*Entity));
	return Respond(OpExportEntity, Result);
}

FString UCKSessionsAgentLibrary::ImportEntity(const FString& AssetPath, const FString& Json, const bool bSave)
{
	using namespace CKSessions::Agent;
	CKSessions::Hub::FEntityType Type;
	FFailure Failure;
	UObject* Entity = ResolveEntity(AssetPath, Type, Failure);
	if (Entity == nullptr)
	{
		return Fail(OpImportEntity, Failure);
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return Fail(OpImportEntity, { TEXT("invalid_json"), TEXT("The text is not a JSON object.") });
	}
	// Accepted shapes: a whole ExportEntity response, its result ({"fields": {...}}) or a flat {"<Field>": "<value>"} object.
	TSharedPtr<FJsonObject> Values = Root;
	const TSharedPtr<FJsonObject>* Nested = nullptr;
	if (Values->TryGetObjectField(TEXT("result"), Nested) && Nested != nullptr && Nested->IsValid())
	{
		Values = *Nested;
	}
	if (Values->TryGetObjectField(TEXT("fields"), Nested) && Nested != nullptr && Nested->IsValid())
	{
		Values = *Nested;
	}

	// First pass: every name and value is checked on a scratch copy, so a bad field changes nothing.
	const TArray<FEntityField> Fields = CollectFields(*Entity);
	TArray<TPair<const FEntityField*, FString>> Changes;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Values->Values)
	{
		const FEntityField* Field = Fields.FindByPredicate([&Entry](const FEntityField& Candidate) { return Candidate.Key.Equals(Entry.Key, ESearchCase::IgnoreCase); });
		if (Field == nullptr)
		{
			return Fail(OpImportEntity, { TEXT("unknown_field"), FString::Printf(TEXT("%s has no editable field %s (DescribeEntity lists them)."), *Type.Entity, *Entry.Key) });
		}
		if (Field->bReadOnly)
		{
			return Fail(OpImportEntity, { TEXT("read_only_field"), FString::Printf(TEXT("%s is read-only."), *Field->Key) });
		}
		FString Text;
		if (!JsonValueToText(Entry.Value, Text))
		{
			return Fail(OpImportEntity, { TEXT("invalid_value"), FString::Printf(TEXT("%s: give the value as a string, a number or a boolean."), *Field->Key) });
		}
		FProperty* Property = Field->Property;
		void* Scratch = FMemory::Malloc(FMath::Max(Property->GetSize(), 1), Property->GetMinAlignment());
		Property->InitializeValue(Scratch);
		Property->CopySingleValue(Scratch, Property->ContainerPtrToValuePtr<void>(Entity, Field->Index));
		FString Error;
		const bool bFits = ImportInto(*Field, *Entity, Scratch, Text, Error);
		Property->DestroyValue(Scratch);
		FMemory::Free(Scratch);
		if (!bFits)
		{
			return Fail(OpImportEntity, { TEXT("invalid_value"), Error });
		}
		Changes.Emplace(Field, Text);
	}

	// Second pass: apply as one undo step, with the editor's change notifications.
	TArray<TSharedPtr<FJsonValue>> Changed;
	if (!Changes.IsEmpty())
	{
		const FScopedTransaction Transaction(LOCTEXT("ImportEntity", "Import entity data"));
		Entity->Modify();
		for (const TPair<const FEntityField*, FString>& Change : Changes)
		{
			const FEntityField& Field = *Change.Key;
			const FString Before = ExportValue(Field, *Entity);
			Entity->PreEditChange(Field.Property);
			FString Error;
			ImportInto(Field, *Entity, Field.Property->ContainerPtrToValuePtr<void>(Entity, Field.Index), Change.Value, Error);
			FPropertyChangedEvent Event(Field.Property, EPropertyChangeType::ValueSet);
			Entity->PostEditChangeProperty(Event);
			if (ExportValue(Field, *Entity) != Before)
			{
				Changed.Add(MakeShared<FJsonValueString>(Field.Key));
			}
		}
	}
	if (bSave && !SaveAsset(*Entity))
	{
		return Fail(OpImportEntity, { TEXT("save_failed"), FString::Printf(TEXT("The values were set but %s was not saved (see the Output Log)."), *Entity->GetPathName()) });
	}
	const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("path"), Entity->GetPathName());
	Result->SetArrayField(TEXT("changed"), Changed);
	Result->SetBoolField(TEXT("saved"), bSave);
	Result->SetObjectField(TEXT("validation"), ValidationJson(*Entity));
	return Respond(OpImportEntity, Result);
}

#undef LOCTEXT_NAMESPACE
