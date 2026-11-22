// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.
// CoopKit shared file: the same hub window ships in every CoopKit plugin with a hub, under the plugin's prefix. CoopKit shared group: hub.

#include "SCKSessionsHub.h"
#include "CKSessionsSkills.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SCKSessionsHub"

namespace CKSessions::Hub::Window
{
	/** Index of the categories and of the entities of one category in the left panel's switcher. */
	constexpr int32 CategoriesPage = 0;
	constexpr int32 EntitiesPage = 1;

	FSlateFontInfo HeadingFont()
	{
		return FCoreStyle::GetDefaultFontStyle("Bold", 13);
	}

	FSlateFontInfo CategoryFont()
	{
		return FCoreStyle::GetDefaultFontStyle("Bold", 11);
	}
}

void SCKSessionsHub::Construct(const FArguments& InArgs)
{
	using namespace CKSessions::Hub::Window;

	const TSharedRef<SWidget> CategoriesPanel = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 4.0f, 4.0f, 2.0f)
		[
			SNew(STextBlock).Font(HeadingFont()).Text(LOCTEXT("Categories", "Categories"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 0.0f, 4.0f, 6.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("CategoriesHint", "Pick what you want to work with."))
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(CategoryBox, SVerticalBox)
			]
		];

	const TSharedRef<SWidget> EntitiesPanel = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Tag(TEXT("CoopKitHub.Back"))
				.Text(LOCTEXT("Back", "< Back"))
				.ToolTipText(LOCTEXT("BackTooltip", "Back to the categories."))
				.OnClicked_Lambda([this]()
				{
					ShowCategories();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock).Font(HeadingFont()).Text_Lambda([this]()
				{
					return FText::Format(LOCTEXT("CategoryTitle", "{0} ({1})"), FText::FromString(Category), FText::AsNumber(CountEntities(Category)));
				})
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 2.0f)
		[
			SAssignNew(SearchBox, SSearchBox)
			.Tag(TEXT("CoopKitHub.Search"))
			.HintText_Lambda([this]() { return FText::Format(LOCTEXT("SearchType", "Search {0}"), FText::FromString(Category)); })
			.OnTextChanged_Lambda([this](const FText& Text)
			{
				SearchText = Text.ToString();
				ApplyFilter();
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 2.0f)
		[
			SAssignNew(NameBox, SEditableTextBox)
			.Tag(TEXT("CoopKitHub.NewName"))
			.HintText(LOCTEXT("NameHint", "Name of a new entity (letters, digits, underscores)"))
			.OnTextChanged_Lambda([this](const FText& Text) { NewName = Text.ToString().TrimStartAndEnd(); })
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Tag(TEXT("CoopKitHub.New"))
				.Text_Lambda([this]() { return FText::Format(LOCTEXT("NewType", "New {0}"), FText::FromString(Category)); })
				.ToolTipText(LOCTEXT("NewTooltip", "Creates an entity of this type with the name on the left: its wizard opens first."))
				.IsEnabled_Lambda([this]() { return GetSelectedType() != nullptr && CKSessions::Hub::IsValidEntityName(NewName); })
				.OnClicked(this, &SCKSessionsHub::OnNew)
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(List, SListView<TSharedPtr<FCKSessionsHubEntry>>)
				.ListItemsSource(&Visible)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow(this, &SCKSessionsHub::MakeRow)
				.OnSelectionChanged(this, &SCKSessionsHub::OnEntrySelected)
				.OnMouseButtonDoubleClick_Lambda([this](TSharedPtr<FCKSessionsHubEntry>) { OnOpen(); })
			]
			+ SOverlay::Slot()
			.Padding(8.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Visibility_Lambda([this]() { return Visible.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				.Text_Lambda([this]()
				{
					return SearchText.IsEmpty() ? LOCTEXT("NoEntities", "No entities of this type yet: type a name above and press New.")
						: LOCTEXT("NoMatches", "No entity matches the search.");
				})
			]
		];

	const TSharedRef<SWidget> DetailsPanel = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 6.0f, 8.0f, 2.0f)
		[
			SNew(STextBlock).Font(HeadingFont()).Text_Lambda([this]() { return SelectedTitle; })
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 2.0f)
		[
			SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]() { return SelectedDetails; })
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Tag(TEXT("CoopKitHub.Open"))
				.Text(LOCTEXT("Open", "Open"))
				.ToolTipText(LOCTEXT("OpenTooltip", "Opens the entity's card in its own window."))
				.IsEnabled_Lambda([this]() { return GetSelectedEntry().IsValid(); })
				.OnClicked(this, &SCKSessionsHub::OnOpen)
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.Tag(TEXT("CoopKitHub.WhereUsed"))
				.Text(LOCTEXT("WhereUsed", "Where Used"))
				.ToolTipText(LOCTEXT("WhereUsedTooltip", "Lists the saved assets that refer to the entity."))
				.IsEnabled_Lambda([this]() { return GetSelectedEntry().IsValid(); })
				.OnClicked(this, &SCKSessionsHub::OnWhereUsed)
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Tag(TEXT("CoopKitHub.CheckProject"))
				.Text(LOCTEXT("Check", "Check Project"))
				.ToolTipText(LOCTEXT("CheckTooltip", "Checks every entity of every type: data errors and duplicate ids."))
				.OnClicked(this, &SCKSessionsHub::OnCheckProject)
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(4.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot().Padding(6.0f)
				[
					SNew(STextBlock).AutoWrapText(true).Text_Lambda([this]() { return Report; })
				]
			]
		];

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(LOCTEXT("Intro", "Entities of every installed CoopKit plugin."))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton).Tag(TEXT("CoopKitHub.Refresh")).Text(LOCTEXT("Refresh", "Refresh")).OnClicked_Lambda([this]()
				{
					Refresh();
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("InstallSkills", "Install AI Agent Skills"))
				.ToolTipText(LOCTEXT("InstallSkillsTooltip", "Copies the AI agent skills of every enabled CoopKit plugin into this project (.claude/skills and .agents/skills), so your AI agent can extend and build the plugins with you."))
				.OnClicked_Lambda([]()
				{
					CKSessions::Skills::RunInstallCommand();
					return FReply::Handled();
				})
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			+ SSplitter::Slot().Value(0.38f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(2.0f)
				[
					SAssignNew(LeftSwitcher, SWidgetSwitcher)
					.WidgetIndex(CategoriesPage)
					+ SWidgetSwitcher::Slot()[CategoriesPanel]
					+ SWidgetSwitcher::Slot()[EntitiesPanel]
				]
			]
			+ SSplitter::Slot().Value(0.62f)
			[
				DetailsPanel
			]
		]
	];
	Refresh();
}

void SCKSessionsHub::Refresh()
{
	// The selection survives the refresh by asset: the entries are made anew.
	const TSharedPtr<FCKSessionsHubEntry> Previous = GetSelectedEntry();
	const FSoftObjectPath PreviousPath = Previous.IsValid() ? Previous->Asset.GetSoftObjectPath() : FSoftObjectPath();

	Types = CKSessions::Hub::CollectEntityTypes();
	Entries.Reset();
	for (const CKSessions::Hub::FEntityType& Type : Types)
	{
		for (const FAssetData& Asset : CKSessions::Hub::FindEntities(Type))
		{
			Entries.Add(MakeShared<FCKSessionsHubEntry>(FCKSessionsHubEntry{ Asset, CKSessions::Hub::GetEntityDisplayName(Asset), Type.Entity, Type.Plugin }));
		}
	}
	Report = FText::Format(LOCTEXT("Summary", "{0} entity types, {1} entities. This copy of the hub (CoopKitSessions, contract v{2}) {3} the tab."),
		FText::AsNumber(Types.Num()), FText::AsNumber(Entries.Num()), FText::AsNumber(CKSessions::Hub::ContractVersion),
		CKSessions::Hub::OwnsHubTab() ? LOCTEXT("Owns", "owns") : LOCTEXT("Shows", "shows"));
	RebuildCategories();
	if (List.IsValid())
	{
		List->ClearSelection();
	}
	if (!Category.IsEmpty() && FindType(Category) == nullptr)
	{
		ShowCategories();
	}
	ApplyFilter();
	OnEntrySelected(nullptr, ESelectInfo::Direct);
	if (PreviousPath.IsValid() && List.IsValid())
	{
		if (const TSharedPtr<FCKSessionsHubEntry>* Found = Visible.FindByPredicate([&PreviousPath](const TSharedPtr<FCKSessionsHubEntry>& Entry) { return Entry->Asset.GetSoftObjectPath() == PreviousPath; }))
		{
			List->SetSelection(*Found);
		}
	}
}

void SCKSessionsHub::RebuildCategories()
{
	using namespace CKSessions::Hub::Window;

	if (!CategoryBox.IsValid())
	{
		return;
	}
	CategoryBox->ClearChildren();
	if (Types.IsEmpty())
	{
		CategoryBox->AddSlot()
		.AutoHeight()
		.Padding(4.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(LOCTEXT("NoTypes", "No entity types: enable a CoopKit plugin with entities (Edit > Plugins)."))
		];
		return;
	}
	for (const CKSessions::Hub::FEntityType& Type : Types)
	{
		const FString Entity = Type.Entity;
		CategoryBox->AddSlot()
		.AutoHeight()
		.Padding(2.0f)
		[
			SNew(SButton)
			.Tag(MakeCategoryTag(Entity))
			.ToolTipText(FText::Format(LOCTEXT("CategoryTooltip", "Show the {0} entities ({1})."), FText::FromString(Entity), FText::FromString(Type.Plugin)))
			.OnClicked_Lambda([this, Entity]()
			{
				ShowCategory(Entity);
				return FReply::Handled();
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(STextBlock).Font(CategoryFont()).Text(FText::FromString(Entity))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(STextBlock).Text(FText::Format(LOCTEXT("CategoryCount", "{0} {0}|plural(one=entity,other=entities)"), FText::AsNumber(CountEntities(Entity))))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground()).Text(FText::FromString(Type.Plugin))
				]
			]
		];
	}
}

bool SCKSessionsHub::ShowCategory(const FString& Entity)
{
	if (FindType(Entity) == nullptr)
	{
		return false;
	}
	Category = Entity;
	SetSearch(FString());
	if (List.IsValid())
	{
		List->ClearSelection();
	}
	ApplyFilter();
	OnEntrySelected(nullptr, ESelectInfo::Direct);
	if (LeftSwitcher.IsValid())
	{
		LeftSwitcher->SetActiveWidgetIndex(CKSessions::Hub::Window::EntitiesPage);
	}
	return true;
}

void SCKSessionsHub::ShowCategories()
{
	Category.Reset();
	SetSearch(FString());
	if (List.IsValid())
	{
		List->ClearSelection();
	}
	ApplyFilter();
	OnEntrySelected(nullptr, ESelectInfo::Direct);
	if (LeftSwitcher.IsValid())
	{
		LeftSwitcher->SetActiveWidgetIndex(CKSessions::Hub::Window::CategoriesPage);
	}
}

int32 SCKSessionsHub::CountEntities(const FString& Entity) const
{
	int32 Count = 0;
	for (const TSharedPtr<FCKSessionsHubEntry>& Entry : Entries)
	{
		Count += Entry->Entity == Entity ? 1 : 0;
	}
	return Count;
}

void SCKSessionsHub::SetSearch(const FString& Text)
{
	SearchText = Text;
	if (SearchBox.IsValid() && SearchBox->GetText().ToString() != Text)
	{
		SearchBox->SetText(FText::FromString(Text));
	}
}

void SCKSessionsHub::ApplyFilter()
{
	Visible.Reset();
	if (!Category.IsEmpty())
	{
		for (const TSharedPtr<FCKSessionsHubEntry>& Entry : Entries)
		{
			if (Entry->Entity == Category && (SearchText.IsEmpty() || Entry->Asset.AssetName.ToString().Contains(SearchText) || Entry->DisplayName.ToString().Contains(SearchText)))
			{
				Visible.Add(Entry);
			}
		}
	}
	if (List.IsValid())
	{
		List->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SCKSessionsHub::MakeRow(TSharedPtr<FCKSessionsHubEntry> Entry, const TSharedRef<STableViewBase>& Owner)
{
	return SNew(STableRow<TSharedPtr<FCKSessionsHubEntry>>, Owner)
		.Padding(FMargin(4.0f, 3.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SNew(STextBlock).Text(Entry->DisplayName)
			]
			+ SHorizontalBox::Slot().FillWidth(0.5f)
			[
				SNew(STextBlock).Text(FText::FromName(Entry->Asset.AssetName)).ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];
}

const CKSessions::Hub::FEntityType* SCKSessionsHub::FindType(const FString& Entity) const
{
	return Types.FindByPredicate([&Entity](const CKSessions::Hub::FEntityType& Type) { return Type.Entity == Entity; });
}

const CKSessions::Hub::FEntityType* SCKSessionsHub::GetSelectedType() const
{
	return Category.IsEmpty() ? nullptr : FindType(Category);
}

TSharedPtr<FCKSessionsHubEntry> SCKSessionsHub::GetSelectedEntry() const
{
	const TArray<TSharedPtr<FCKSessionsHubEntry>> Selected = List.IsValid() ? List->GetSelectedItems() : TArray<TSharedPtr<FCKSessionsHubEntry>>();
	return Selected.IsEmpty() ? nullptr : Selected[0];
}

void SCKSessionsHub::OnEntrySelected(TSharedPtr<FCKSessionsHubEntry> Entry, ESelectInfo::Type SelectInfo)
{
	if (Entry.IsValid())
	{
		SelectedTitle = Entry->DisplayName;
		SelectedDetails = DescribeEntry(*Entry);
		return;
	}
	SelectedTitle = LOCTEXT("NothingSelected", "No entity selected");
	SelectedDetails = Category.IsEmpty() ? LOCTEXT("PickCategory", "Pick a category on the left, then an entity.")
		: LOCTEXT("PickEntity", "Pick an entity on the left, or create one: type its name and press New.");
}

FText SCKSessionsHub::DescribeEntry(const FCKSessionsHubEntry& Entry) const
{
	FTextBuilder Builder;
	Builder.AppendLine(FText::Format(LOCTEXT("DetailsType", "Type: {0} ({1})"), FText::FromString(Entry.Entity), FText::FromString(Entry.Plugin)));
	Builder.AppendLine(FText::Format(LOCTEXT("DetailsAsset", "Asset: {0}"), FText::FromName(Entry.Asset.PackageName)));
	const CKSessions::Hub::FEntityType* Type = FindType(Entry.Entity);
	if (Type != nullptr && !Type->IdProperty.IsNone())
	{
		const FNameProperty* IdProperty = FindFProperty<FNameProperty>(Type->Class, Type->IdProperty);
		const UObject* Entity = Entry.Asset.GetAsset();
		if (IdProperty != nullptr && Entity != nullptr && Entity->IsA(Type->Class))
		{
			Builder.AppendLine(FText::Format(LOCTEXT("DetailsId", "Id: {0}"), FText::FromName(IdProperty->GetPropertyValue_InContainer(Entity))));
		}
	}
	Builder.AppendLine(LOCTEXT("DetailsActions", "Open shows its card in a new window; double-click in the list does the same."));
	return Builder.ToText();
}

FReply SCKSessionsHub::OnNew()
{
	if (const CKSessions::Hub::FEntityType* Type = GetSelectedType())
	{
		// A copy: the refresh after creating rebuilds the type list the pointer points into.
		const CKSessions::Hub::FEntityType Selected = *Type;
		CreateNewEntity(Selected, NewName);
	}
	return FReply::Handled();
}

UObject* SCKSessionsHub::CreateNewEntity(const CKSessions::Hub::FEntityType& Type, const FString& Name, UFactory* Factory, const bool bSkipWizard)
{
	UObject* Entity = CKSessions::Hub::CreateEntity(Type, Name, Factory, bSkipWizard);
	if (Entity == nullptr)
	{
		Report = LOCTEXT("NotCreated", "Nothing created: the name is taken, the type has no factory or its wizard was cancelled.");
		return nullptr;
	}
	CKSessions::Hub::OpenCard(Entity);
	// The refresh lists the new entity and writes the summary; "Created" comes after it, so the summary does not hide it.
	Refresh();
	if (NameBox.IsValid())
	{
		NameBox->SetText(FText::GetEmpty());
	}
	Report = FText::Format(LOCTEXT("Created", "Created {0}."), FText::FromString(Entity->GetPathName()));
	SelectEntity(*Entity);
	return Entity;
}

void SCKSessionsHub::SelectEntity(const UObject& Entity)
{
	const FName Package = Entity.GetOutermost()->GetFName();
	const FName Asset = Entity.GetFName();
	const TFunction<TSharedPtr<FCKSessionsHubEntry>(const TArray<TSharedPtr<FCKSessionsHubEntry>>&)> Find = [Package, Asset](const TArray<TSharedPtr<FCKSessionsHubEntry>>& Items)
	{
		const TSharedPtr<FCKSessionsHubEntry>* Found = Items.FindByPredicate([Package, Asset](const TSharedPtr<FCKSessionsHubEntry>& Item)
		{
			return Item->Asset.PackageName == Package && Item->Asset.AssetName == Asset;
		});
		return Found != nullptr ? *Found : TSharedPtr<FCKSessionsHubEntry>();
	};
	const TSharedPtr<FCKSessionsHubEntry> Known = Find(Entries);
	if (!Known.IsValid())
	{
		return;
	}
	if (Known->Entity != Category)
	{
		ShowCategory(Known->Entity);
	}
	TSharedPtr<FCKSessionsHubEntry> Entry = Find(Visible);
	if (!Entry.IsValid())
	{
		// The search hides the entity: clear it, so the entity shows.
		SetSearch(FString());
		ApplyFilter();
		Entry = Find(Visible);
	}
	if (Entry.IsValid() && List.IsValid())
	{
		List->SetSelection(Entry);
		List->RequestScrollIntoView(Entry);
	}
}

FReply SCKSessionsHub::OnOpen()
{
	if (const TSharedPtr<FCKSessionsHubEntry> Entry = GetSelectedEntry())
	{
		CKSessions::Hub::OpenCard(Entry->Asset.GetAsset());
	}
	return FReply::Handled();
}

FReply SCKSessionsHub::OnWhereUsed()
{
	if (const TSharedPtr<FCKSessionsHubEntry> Entry = GetSelectedEntry())
	{
		const TArray<FName> Referencers = CKSessions::Hub::FindReferencers(Entry->Asset);
		FTextBuilder Builder;
		Builder.AppendLine(FText::Format(LOCTEXT("UsedBy", "{0} is used by {1} {1}|plural(one=asset,other=assets) (saved data):"),
			FText::FromName(Entry->Asset.AssetName), FText::AsNumber(Referencers.Num())));
		for (const FName Referencer : Referencers)
		{
			Builder.AppendLine(FText::FromName(Referencer));
		}
		Report = Builder.ToText();
	}
	return FReply::Handled();
}

FReply SCKSessionsHub::OnCheckProject()
{
	const TArray<CKSessions::Hub::FProjectIssue> Issues = CKSessions::Hub::CheckProject();
	FTextBuilder Builder;
	Builder.AppendLine(Issues.IsEmpty() ? LOCTEXT("Clean", "Every entity of every type is valid; no duplicate ids.")
		: FText::Format(LOCTEXT("Issues", "{0} {0}|plural(one=problem,other=problems):"), FText::AsNumber(Issues.Num())));
	for (const CKSessions::Hub::FProjectIssue& Issue : Issues)
	{
		Builder.AppendLine(FText::Format(LOCTEXT("Issue", "{0} {1}: {2}"), Issue.bError ? LOCTEXT("Error", "Error") : LOCTEXT("Warning", "Warning"),
			FText::FromString(Issue.Asset.GetAssetName()), Issue.Message));
	}
	Report = Builder.ToText();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
