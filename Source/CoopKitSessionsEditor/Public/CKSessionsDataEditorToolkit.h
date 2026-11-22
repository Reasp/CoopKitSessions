// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CKSessionsDataValidation.h"
#include "Templates/Function.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/SWidget.h"

class IDetailsView;
class SDockTab;
struct FPropertyChangedEvent;

/**
 * Data editor window of CoopKitSessions, shared by every data type of the plugin. Tabs:
 *   - Properties: the asset's editable properties;
 *   - Summary: a mechanic-specific overview built by the data type's summary function;
 *   - Validation: the asset's IsDataValid errors and warnings, refreshed on every property change.
 * An asset definition opens it from OpenAssets with the summary function of its data type.
 */
class COOPKITSESSIONSEDITOR_API FCKSessionsDataEditorToolkit final : public FAssetEditorToolkit
{
public:
	/** Builds the Summary tab text for one asset of the data type. */
	using FSummaryBuilder = TFunction<FText(const UObject&)>;

	/** Name of the editor, as IAssetEditorInstance::GetEditorName reports it. */
	static const FName ToolkitName;
	static const FName PropertiesTabId;
	static const FName SummaryTabId;
	static const FName ValidationTabId;

	/** A tab a data type adds to the window (the card adds Preview and Parts). */
	struct FExtraTab
	{
		FName Id;
		FText Label;

		/** Builds the tab's content for the edited asset. */
		TFunction<TSharedRef<SWidget>(UObject&)> MakeContent;
	};

	explicit FCKSessionsDataEditorToolkit(FSummaryBuilder InSummaryBuilder);

	/** Adds a tab before InitDataEditor; extra tabs stand between Properties and Summary/Validation. */
	void AddExtraTab(FExtraTab Tab);

	/** Ids of the extra tabs of the window. */
	TArray<FName> GetExtraTabIds() const;
	virtual ~FCKSessionsDataEditorToolkit() override;

	/**
	 * Opens the window for one asset.
	 * @param Mode         Standalone or world-centric, as the asset definition receives it.
	 * @param InToolkitHost  Host for world-centric mode; may be empty.
	 * @param Data         Asset to edit; must not be null.
	 */
	void InitDataEditor(EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InToolkitHost, UObject* Data);

	/** Current text of the Summary tab. */
	FText GetSummaryText() const;

	/** Result of the last validation, shown in the Validation tab. */
	const CKSessions::Editor::FValidationReport& GetValidationReport() const
	{
		return ValidationReport;
	}

	/** Validates the asset again and updates the Validation tab. */
	void RefreshValidation();

	// FAssetEditorToolkit interface
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;

private:
	TSharedRef<SDockTab> SpawnPropertiesTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnSummaryTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnValidationTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnExtraTab(const FSpawnTabArgs& Args, int32 TabIndex);
	void OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);

	FSummaryBuilder SummaryBuilder;
	TArray<FExtraTab> ExtraTabs;
	TWeakObjectPtr<UObject> EditedData;
	TSharedPtr<IDetailsView> DetailsView;
	CKSessions::Editor::FValidationReport ValidationReport;
	FDelegateHandle PropertyChangedHandle;
};
