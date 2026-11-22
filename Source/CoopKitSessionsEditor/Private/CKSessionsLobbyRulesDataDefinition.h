// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "AssetDefinitionDefault.h"
#include "CoreMinimal.h"
#include "CKSessionsLobbyRulesDataDefinition.generated.h"

/** How lobby rules look in the Content Browser (CoopKit category, color) and that they open in the data editor. */
UCLASS()
class UCKSessionsLobbyRulesDataDefinition final : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	virtual FText GetAssetDisplayName() const override;
	virtual FLinearColor GetAssetColor() const override;
	virtual TSoftClassPtr<UObject> GetAssetClass() const override;
	virtual TConstArrayView<FAssetCategoryPath> GetAssetCategories() const override;
	virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};
