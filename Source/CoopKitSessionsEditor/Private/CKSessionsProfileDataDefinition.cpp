// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsProfileDataDefinition.h"
#include "CKSessionsDataEditorToolkit.h"
#include "CKSessionsDataSummary.h"
#include "CKSessionsProfileData.h"
#include "SCKSessionsCard.h"

#define LOCTEXT_NAMESPACE "CKSessionsProfileDataDefinition"

const FName UCKSessionsProfileDataDefinition::PreviewTabId(TEXT("CKSessionsCard_Preview"));
const FName UCKSessionsProfileDataDefinition::PartsTabId(TEXT("CKSessionsCard_Parts"));

FText UCKSessionsProfileDataDefinition::GetAssetDisplayName() const
{
	return LOCTEXT("DisplayName", "Sessions Profile");
}

FLinearColor UCKSessionsProfileDataDefinition::GetAssetColor() const
{
	return FLinearColor(0.2f, 0.45f, 0.8f);
}

TSoftClassPtr<UObject> UCKSessionsProfileDataDefinition::GetAssetClass() const
{
	return UCKSessionsProfileData::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UCKSessionsProfileDataDefinition::GetAssetCategories() const
{
	// Every CoopKit plugin uses the same top-level category, so all CoopKit data sits together in Add > CoopKit.
	static const FAssetCategoryPath Categories[] = { FAssetCategoryPath(LOCTEXT("CoopKitCategory", "CoopKit")) };
	return Categories;
}

EAssetCommandResult UCKSessionsProfileDataDefinition::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UCKSessionsProfileData* Data : OpenArgs.LoadObjects<UCKSessionsProfileData>())
	{
		const TSharedRef<FCKSessionsDataEditorToolkit> Editor = MakeShared<FCKSessionsDataEditorToolkit>(&CKSessions::Editor::BuildProfileDataSummary);
		// The card: the data editor plus the profile's look and every part of it in other assets (a profile has none).
		Editor->AddExtraTab({ PreviewTabId, LOCTEXT("PreviewTab", "Preview"), [](UObject& Edited) -> TSharedRef<SWidget>
		{
			return SNew(SCKSessionsPreview, &Edited);
		} });
		Editor->AddExtraTab({ PartsTabId, LOCTEXT("PartsTab", "Parts"), [](UObject& Edited) -> TSharedRef<SWidget>
		{
			return SNew(SCKSessionsParts, &Edited);
		} });
		Editor->InitDataEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, Data);
	}
	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
