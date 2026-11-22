// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsLobbyRulesDataDefinition.h"
#include "CKSessionsDataEditorToolkit.h"
#include "CKSessionsDataSummary.h"
#include "CKSessionsLobbyRulesData.h"

#define LOCTEXT_NAMESPACE "CKSessionsLobbyRulesDataDefinition"

FText UCKSessionsLobbyRulesDataDefinition::GetAssetDisplayName() const
{
	return LOCTEXT("DisplayName", "Sessions Lobby Rules");
}

FLinearColor UCKSessionsLobbyRulesDataDefinition::GetAssetColor() const
{
	return FLinearColor(0.25f, 0.65f, 0.55f);
}

TSoftClassPtr<UObject> UCKSessionsLobbyRulesDataDefinition::GetAssetClass() const
{
	return UCKSessionsLobbyRulesData::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UCKSessionsLobbyRulesDataDefinition::GetAssetCategories() const
{
	// Every CoopKit plugin uses the same top-level category, so all CoopKit data sits together in Add > CoopKit.
	static const FAssetCategoryPath Categories[] = { FAssetCategoryPath(LOCTEXT("CoopKitCategory", "CoopKit")) };
	return Categories;
}

EAssetCommandResult UCKSessionsLobbyRulesDataDefinition::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	for (UCKSessionsLobbyRulesData* Data : OpenArgs.LoadObjects<UCKSessionsLobbyRulesData>())
	{
		const TSharedRef<FCKSessionsDataEditorToolkit> Editor = MakeShared<FCKSessionsDataEditorToolkit>(&CKSessions::Editor::BuildLobbyRulesDataSummary);
		Editor->InitDataEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, Data);
	}
	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
