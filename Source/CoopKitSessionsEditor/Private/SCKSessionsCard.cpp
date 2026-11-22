// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#include "SCKSessionsCard.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetThumbnail.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "IContentBrowserSingleton.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SCKSessionsCard"

namespace
{
	using FCKSessionsEntityPart = CKSessions::Workspace::FCKSessionsEntityPart;

	FAssetData FindAssetData(const FSoftObjectPath& Path)
	{
		if (Path.IsNull())
		{
			return FAssetData();
		}
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		return Registry.GetAssetByObjectPath(Path);
	}
}

void SCKSessionsParts::Construct(const FArguments& InArgs, UObject* InEntity)
{
	Entity = InEntity;
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SCKSessionsParts::OnObjectPropertyChanged);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(LOCTEXT("PartsIntro", "Every part of the entity in other assets. Own parts lie in the entity's folder and rename, duplicate and delete with it; shared parts are never touched."))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f, 0.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(FLinearColor(1.0f, 0.75f, 0.3f))
			.Text_Lambda([this]() { return KitWarnings; })
			.Visibility_Lambda([this]() { return KitWarnings.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(LOCTEXT("NoPartSlots", "This entity has no parts: everything it needs is in its own properties."))
			.Visibility_Lambda([this]() { return Items.Num() == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(List, SListView<TSharedPtr<FCKSessionsEntityPart>>)
			.ListItemsSource(&Items)
			.SelectionMode(ESelectionMode::None)
			.OnGenerateRow(this, &SCKSessionsParts::MakeRow)
		]
	];
	Refresh();
}

SCKSessionsParts::~SCKSessionsParts()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
}

void SCKSessionsParts::Refresh()
{
	Items.Reset();
	KitWarnings = FText::GetEmpty();
	if (const UObject* Data = Entity.Get())
	{
		for (const FCKSessionsEntityPart& Part : CKSessions::Workspace::CollectParts(*Data))
		{
			Items.Add(MakeShared<FCKSessionsEntityPart>(Part));
		}
		const CKSessions::Workspace::FCKSessionsEntityKit Kit = CKSessions::Workspace::CollectKit(*Data);
		// A part in the entity's folder that another asset uses is shared: badge it like CollectKit treats it.
		for (const TSharedPtr<FCKSessionsEntityPart>& Item : Items)
		{
			if (Item->bOwn && Kit.Shared.Contains(Item->Asset))
			{
				Item->bOwn = false;
			}
		}
		if (!Kit.Warnings.IsEmpty())
		{
			FTextBuilder Builder;
			for (const FText& Warning : Kit.Warnings)
			{
				Builder.AppendLine(Warning);
			}
			KitWarnings = Builder.ToText();
		}
	}
	if (List.IsValid())
	{
		List->RequestListRefresh();
	}
}

void SCKSessionsParts::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	if (Object != nullptr && Object == Entity.Get())
	{
		Refresh();
	}
}

TSharedRef<ITableRow> SCKSessionsParts::MakeRow(TSharedPtr<FCKSessionsEntityPart> Part, const TSharedRef<STableViewBase>& Owner)
{
	const TSharedRef<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(FindAssetData(Part->Asset), 48, 48, ThumbnailPool);
	const FText Status = Part->Asset.IsNull() ? LOCTEXT("Empty", "empty") : Part->bOwn ? LOCTEXT("Own", "own") : LOCTEXT("Shared", "shared");
	const FLinearColor StatusColor = Part->Asset.IsNull() ? FLinearColor(0.5f, 0.5f, 0.5f) : Part->bOwn ? FLinearColor(0.4f, 0.85f, 0.45f) : FLinearColor(0.45f, 0.7f, 1.0f);
	TWeakObjectPtr<UObject> WeakEntity = Entity;
	TWeakPtr<SCKSessionsParts> WeakParts = StaticCastSharedRef<SCKSessionsParts>(AsShared());

	return SNew(STableRow<TSharedPtr<FCKSessionsEntityPart>>, Owner)
		.Padding(FMargin(4.0f, 2.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f)
			[
				SNew(SBox).WidthOverride(48.0f).HeightOverride(48.0f)
				[
					Thumbnail->MakeThumbnailWidget()
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(Part->Label)
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Text(Part->Asset.IsNull() ? LOCTEXT("NoAsset", "(none)") : FText::FromString(Part->Asset.GetAssetName()))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f)
			[
				SNew(STextBlock).Text(Status).ColorAndOpacity(StatusColor)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text(LOCTEXT("Open", "Open"))
				.ToolTipText(LOCTEXT("OpenTip", "Open the part in its own editor."))
				.IsEnabled(!Part->Asset.IsNull())
				.OnClicked_Lambda([Part]()
				{
					if (UObject* Object = Part->Asset.TryLoad())
					{
						GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Object);
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SComboButton)
				.ButtonContent()[SNew(STextBlock).Text(LOCTEXT("Replace", "Replace"))]
				.ToolTipText(LOCTEXT("ReplaceTip", "Point the slot at another asset."))
				.OnGetMenuContent(this, &SCKSessionsParts::MakeReplaceMenu, Part)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text(LOCTEXT("Create", "Create"))
				.ToolTipText(Part->bCanCreate ? LOCTEXT("CreateTip", "Create a new own asset for the slot in the entity's folder and use it.")
					: LOCTEXT("CreateImportTip", "Import or pick this kind of asset: the card cannot create it."))
				.IsEnabled(Part->bCanCreate)
				.OnClicked_Lambda([Part, WeakEntity, WeakParts]()
				{
					if (UObject* Data = WeakEntity.Get())
					{
						if (UObject* Created = CKSessions::Workspace::CreatePart(*Data, *Part))
						{
							GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Created);
						}
					}
					if (const TSharedPtr<SCKSessionsParts> Pinned = WeakParts.Pin())
					{
						Pinned->Refresh();
					}
					return FReply::Handled();
				})
			]
		];
}

TSharedRef<SWidget> SCKSessionsParts::MakeReplaceMenu(TSharedPtr<FCKSessionsEntityPart> Part)
{
	TWeakObjectPtr<UObject> WeakEntity = Entity;
	TWeakPtr<SCKSessionsParts> WeakParts = StaticCastSharedRef<SCKSessionsParts>(AsShared());

	FAssetPickerConfig Config;
	Config.Filter.ClassPaths.Add(Part->AssetClass != nullptr ? Part->AssetClass->GetClassPathName() : UObject::StaticClass()->GetClassPathName());
	Config.Filter.bRecursiveClasses = true;
	Config.InitialAssetViewType = EAssetViewType::List;
	Config.bAllowNullSelection = true;
	Config.InitialAssetSelection = FAssetData();
	Config.OnAssetSelected = FOnAssetSelected::CreateLambda([Part, WeakEntity, WeakParts](const FAssetData& Asset)
	{
		if (UObject* Data = WeakEntity.Get())
		{
			CKSessions::Workspace::ReplacePart(*Data, *Part, Asset.IsValid() ? Asset.GetAsset() : nullptr);
		}
		if (const TSharedPtr<SCKSessionsParts> Pinned = WeakParts.Pin())
		{
			Pinned->Refresh();
		}
		FSlateApplication::Get().DismissAllMenus();
	});
	FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	return SNew(SBox).WidthOverride(360.0f).HeightOverride(420.0f)[ContentBrowser.Get().CreateAssetPicker(Config)];
}

void SCKSessionsPreview::Construct(const FArguments& InArgs, UObject* InEntity)
{
	Entity = InEntity;
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(16);
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &SCKSessionsPreview::OnObjectPropertyChanged);
	Rebuild();
}

SCKSessionsPreview::~SCKSessionsPreview()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
}

void SCKSessionsPreview::Rebuild()
{
	Previewed = FAssetData();
	FText Title = LOCTEXT("NoEntity", "No entity.");
	bool bHasPartSlots = false;
	if (const UObject* Data = Entity.Get())
	{
		Title = FText::FromString(CKSessions::Workspace::GetEntityName(*Data));
		const TArray<FCKSessionsEntityPart> Parts = CKSessions::Workspace::CollectParts(*Data);
		bHasPartSlots = Parts.Num() > 0;
		for (const FCKSessionsEntityPart& Part : Parts)
		{
			Previewed = FindAssetData(Part.Asset);
			if (Previewed.IsValid())
			{
				break;
			}
		}
	}
	TSharedRef<SWidget> Picture = SNew(STextBlock).AutoWrapText(true).Text(bHasPartSlots
		? LOCTEXT("NoPart", "No part is set yet: pick one in the Parts tab.")
		: LOCTEXT("NoPartSlots", "This entity has no parts: everything it needs is in its own properties."));
	if (Previewed.IsValid())
	{
		const TSharedRef<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(Previewed, 160, 160, ThumbnailPool);
		Picture = SNew(SBox).WidthOverride(160.0f).HeightOverride(160.0f)[Thumbnail->MakeThumbnailWidget()];
	}
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f)
		[
			SNew(STextBlock).Text(Title).Font(FAppStyle::GetFontStyle("BoldFont"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8.0f)
		[
			Picture
		]
	];
}

void SCKSessionsPreview::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	if (Object != nullptr && Object == Entity.Get())
	{
		Rebuild();
	}
}

#undef LOCTEXT_NAMESPACE
