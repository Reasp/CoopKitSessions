// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "CKSessionsDataEditorToolkit.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "CKSessionsDataEditorToolkit"

const FName FCKSessionsDataEditorToolkit::ToolkitName(TEXT("CKSessionsDataEditor"));
const FName FCKSessionsDataEditorToolkit::PropertiesTabId(TEXT("CKSessionsDataEditor_Properties"));
const FName FCKSessionsDataEditorToolkit::SummaryTabId(TEXT("CKSessionsDataEditor_Summary"));
const FName FCKSessionsDataEditorToolkit::ValidationTabId(TEXT("CKSessionsDataEditor_Validation"));

FCKSessionsDataEditorToolkit::FCKSessionsDataEditorToolkit(FSummaryBuilder InSummaryBuilder)
	: SummaryBuilder(MoveTemp(InSummaryBuilder))
{
}

FCKSessionsDataEditorToolkit::~FCKSessionsDataEditorToolkit()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
}

void FCKSessionsDataEditorToolkit::InitDataEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InToolkitHost, UObject* Data)
{
	check(Data != nullptr);
	EditedData = Data;

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsView = PropertyEditor.CreateDetailView(DetailsArgs);
	DetailsView->SetObject(Data);

	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FCKSessionsDataEditorToolkit::OnObjectPropertyChanged);
	RefreshValidation();

	// Properties on the left; Summary and Validation stacked on the right. The layout name carries a version suffix:
	// change it when the layout changes, so editors do not restore an outdated saved layout.
	if (!ExtraTabs.IsEmpty())
	{
		// The card: Properties, the data type's own tabs in the middle (each in its stack), Summary and Validation right.
		TSharedRef<FTabManager::FSplitter> Middle = FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)->SetSizeCoefficient(0.4f);
		for (int32 Index = 0; Index < ExtraTabs.Num(); ++Index)
		{
			// The first extra tab (the preview) takes 40% of the column, the rest share 60% (the parts list is long).
			const float Size = ExtraTabs.Num() == 1 ? 1.0f : Index == 0 ? 0.4f : 0.6f / static_cast<float>(ExtraTabs.Num() - 1);
			Middle->Split(FTabManager::NewStack()->SetSizeCoefficient(Size)->AddTab(ExtraTabs[Index].Id, ETabState::OpenedTab));
		}
		const TSharedRef<FTabManager::FLayout> CardLayout = FTabManager::NewLayout(TEXT("CKSessionsDataEditor_CardLayout_v2"))
			->AddArea
			(
				FTabManager::NewPrimaryArea()
				->SetOrientation(Orient_Horizontal)
				->Split(FTabManager::NewStack()->SetSizeCoefficient(0.35f)->AddTab(PropertiesTabId, ETabState::OpenedTab))
				->Split(Middle)
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.25f)
					->Split(FTabManager::NewStack()->AddTab(SummaryTabId, ETabState::OpenedTab))
					->Split(FTabManager::NewStack()->AddTab(ValidationTabId, ETabState::OpenedTab))
				)
			);
		InitAssetEditor(Mode, InToolkitHost, ToolkitName, CardLayout, true, true, Data);
		return;
	}
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout(TEXT("CKSessionsDataEditor_Layout_v1"))
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.55f)
				->AddTab(PropertiesTabId, ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.45f)
				->Split
				(
					FTabManager::NewStack()
					->AddTab(SummaryTabId, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->AddTab(ValidationTabId, ETabState::OpenedTab)
				)
			)
		);

	InitAssetEditor(Mode, InToolkitHost, ToolkitName, Layout, true, true, Data);
}

void FCKSessionsDataEditorToolkit::AddExtraTab(FExtraTab Tab)
{
	ExtraTabs.Add(MoveTemp(Tab));
}

TArray<FName> FCKSessionsDataEditorToolkit::GetExtraTabIds() const
{
	TArray<FName> Ids;
	for (const FExtraTab& Tab : ExtraTabs)
	{
		Ids.Add(Tab.Id);
	}
	return Ids;
}

TSharedRef<SDockTab> FCKSessionsDataEditorToolkit::SpawnExtraTab(const FSpawnTabArgs& Args, const int32 TabIndex)
{
	UObject* Data = EditedData.Get();
	const FExtraTab& Tab = ExtraTabs[TabIndex];
	return SNew(SDockTab)
		.Label(Tab.Label)
		[
			Data != nullptr && Tab.MakeContent ? Tab.MakeContent(*Data) : SNullWidget::NullWidget
		];
}

FText FCKSessionsDataEditorToolkit::GetSummaryText() const
{
	const UObject* Data = EditedData.Get();
	if (Data == nullptr)
	{
		return LOCTEXT("NoData", "The asset is no longer loaded.");
	}
	return SummaryBuilder ? SummaryBuilder(*Data) : FText::FromString(Data->GetName());
}

void FCKSessionsDataEditorToolkit::RefreshValidation()
{
	if (const UObject* Data = EditedData.Get())
	{
		ValidationReport = CKSessions::Editor::ValidateData(*Data);
	}
}

void FCKSessionsDataEditorToolkit::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	if (Object != nullptr && Object == EditedData.Get())
	{
		RefreshValidation();
	}
}

void FCKSessionsDataEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu", "CoopKitSessions Data"));
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(PropertiesTabId, FOnSpawnTab::CreateSP(this, &FCKSessionsDataEditorToolkit::SpawnPropertiesTab))
		.SetDisplayName(LOCTEXT("PropertiesTab", "Properties"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("LevelEditor.Tabs.Details")));
	InTabManager->RegisterTabSpawner(SummaryTabId, FOnSpawnTab::CreateSP(this, &FCKSessionsDataEditorToolkit::SpawnSummaryTab))
		.SetDisplayName(LOCTEXT("SummaryTab", "Summary"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());
	InTabManager->RegisterTabSpawner(ValidationTabId, FOnSpawnTab::CreateSP(this, &FCKSessionsDataEditorToolkit::SpawnValidationTab))
		.SetDisplayName(LOCTEXT("ValidationTab", "Validation"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());
	for (int32 Index = 0; Index < ExtraTabs.Num(); ++Index)
	{
		InTabManager->RegisterTabSpawner(ExtraTabs[Index].Id, FOnSpawnTab::CreateSP(this, &FCKSessionsDataEditorToolkit::SpawnExtraTab, Index))
			.SetDisplayName(ExtraTabs[Index].Label)
			.SetGroup(WorkspaceMenuCategory.ToSharedRef());
	}
}

void FCKSessionsDataEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
	InTabManager->UnregisterTabSpawner(PropertiesTabId);
	InTabManager->UnregisterTabSpawner(SummaryTabId);
	InTabManager->UnregisterTabSpawner(ValidationTabId);
	for (const FExtraTab& Tab : ExtraTabs)
	{
		InTabManager->UnregisterTabSpawner(Tab.Id);
	}
}

TSharedRef<SDockTab> FCKSessionsDataEditorToolkit::SpawnPropertiesTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("PropertiesTab", "Properties"))
		[
			DetailsView.ToSharedRef()
		];
}

TSharedRef<SDockTab> FCKSessionsDataEditorToolkit::SpawnSummaryTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("SummaryTab", "Summary"))
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			.Padding(8.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([this]() { return GetSummaryText(); })
			]
		];
}

TSharedRef<SDockTab> FCKSessionsDataEditorToolkit::SpawnValidationTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("ValidationTab", "Validation"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(8.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ValidateButton", "Validate"))
				.ToolTipText(LOCTEXT("ValidateTooltip", "Run the asset's data validation again. It also runs after every property change."))
				.OnClicked_Lambda([this]()
				{
					RefreshValidation();
					return FReply::Handled();
				})
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(8.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text_Lambda([this]() { return ValidationReport.ToText(); })
					.ColorAndOpacity_Lambda([this]()
					{
						return ValidationReport.Result == EDataValidationResult::Invalid ? FSlateColor(FLinearColor(1.0f, 0.35f, 0.3f)) : FSlateColor::UseForeground();
					})
				]
			]
		];
}

FName FCKSessionsDataEditorToolkit::GetToolkitFName() const
{
	return ToolkitName;
}

FText FCKSessionsDataEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "CoopKitSessions Data Editor");
}

FString FCKSessionsDataEditorToolkit::GetWorldCentricTabPrefix() const
{
	return TEXT("CoopKitSessions Data ");
}

FLinearColor FCKSessionsDataEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.2f, 0.45f, 0.8f, 0.5f);
}

#undef LOCTEXT_NAMESPACE
