// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "AssetDefinitionDefault.h"
#include "CoreMinimal.h"
#include "CKSessionsProfileDataDefinition.generated.h"

/**
 * How session profiles look in the Content Browser (CoopKit category, color) and that they open in the card: the data editor
 * plus the Preview and Parts tabs of the CoopKit workspace (doc-57).
 */
UCLASS()
class UCKSessionsProfileDataDefinition final : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	/** Ids of the card's own tabs, as FCKSessionsDataEditorToolkit::GetExtraTabIds reports them. */
	static const FName PreviewTabId;
	static const FName PartsTabId;

	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};
