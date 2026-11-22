// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.
// CoopKit shared file: the same hub window ships in every CoopKit plugin with a hub, under the plugin's prefix. CoopKit shared group: hub.

#pragma once

#include "AssetRegistry/AssetData.h"
#include "CKSessionsHub.h"
#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class SEditableTextBox;
class SSearchBox;
class SVerticalBox;
class SWidgetSwitcher;
class UFactory;

/** One entity in the hub list: the asset and its type. */
struct FCKSessionsHubEntry
{
	FAssetData Asset;
	FText DisplayName;
	FString Entity;
	FString Plugin;
};

/**
 * The CoopKit Hub window (tab CoopKitHub, contract version 2): the entities of every installed CoopKit plugin.
 *
 * The left panel first lists the categories (entity types) with the number of entities of each. A click on a category
 * lists only its entities, with a search, a New button for that type and a Back button to the categories. The right
 * panel shows the selected entity and its actions: Open (the entity's card, in its own window), Where Used and Check
 * Project; their results show below them. Everything works with the mouse; only the name of a new entity is typed.
 *
 * Widget tags (SWidget::GetTag) for tests and screenshots: CoopKitHub.Category.<Entity>, CoopKitHub.Back,
 * CoopKitHub.Search, CoopKitHub.NewName, CoopKitHub.New, CoopKitHub.Open, CoopKitHub.WhereUsed,
 * CoopKitHub.CheckProject, CoopKitHub.Refresh.
 */
class COOPKITSESSIONSEDITOR_API SCKSessionsHub final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCKSessionsHub) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Collects the types and entities again and writes the summary below the actions; the open category stays open. */
	void Refresh();

	/**
	 * What New does: creates an entity of the type through its factory (its wizard runs unless bSkipWizard), opens its
	 * card, opens the type's category, selects the entity and says "Created ..." below the actions. A search that hides
	 * the new entity is cleared. Returns the entity, or null when nothing was created.
	 */
	UObject* CreateNewEntity(const CKSessions::Hub::FEntityType& Type, const FString& Name, UFactory* Factory = nullptr, bool bSkipWizard = false);

	/** What a category button does: the left panel lists the entities of the type, search cleared. False for an unknown type. */
	bool ShowCategory(const FString& Entity);

	/** What Back does: the left panel lists the categories again. */
	void ShowCategories();

	/** Opens the entity's category when needed and selects the entity in the list (a search that hides it is cleared). */
	void SelectEntity(const UObject& Entity);

	/** The type whose entities the left panel lists; empty while it lists the categories. */
	const FString& GetCategory() const
	{
		return Category;
	}

	/** Number of entities of a type, as its category button shows it. */
	int32 CountEntities(const FString& Entity) const;

	/** The text below the actions (summary, Created, Where Used, Check Project), for tests. */
	const FText& GetReport() const
	{
		return Report;
	}

	/** The entity selected in the list, or null. */
	TSharedPtr<FCKSessionsHubEntry> GetSelectedEntry() const;

	/** Entity types the hub shows as categories (CoopKitEntity of every plugin), for tests. */
	const TArray<CKSessions::Hub::FEntityType>& GetTypes() const
	{
		return Types;
	}

	/** Every entity of every type the hub knows, whatever the left panel shows. */
	const TArray<TSharedPtr<FCKSessionsHubEntry>>& GetEntries() const
	{
		return Entries;
	}

	/** Entities the left panel lists now: those of the open category that match the search; empty on the categories. */
	const TArray<TSharedPtr<FCKSessionsHubEntry>>& GetVisibleEntries() const
	{
		return Visible;
	}

	/** Tag of the button of a category. */
	static FName MakeCategoryTag(const FString& Entity)
	{
		return FName(*(TEXT("CoopKitHub.Category.") + Entity));
	}

private:
	void RebuildCategories();
	void ApplyFilter();
	TSharedRef<ITableRow> MakeRow(TSharedPtr<FCKSessionsHubEntry> Entry, const TSharedRef<STableViewBase>& Owner);
	const CKSessions::Hub::FEntityType* FindType(const FString& Entity) const;
	const CKSessions::Hub::FEntityType* GetSelectedType() const;
	void OnEntrySelected(TSharedPtr<FCKSessionsHubEntry> Entry, ESelectInfo::Type SelectInfo);
	FText DescribeEntry(const FCKSessionsHubEntry& Entry) const;
	void SetSearch(const FString& Text);
	FReply OnNew();
	FReply OnOpen();
	FReply OnWhereUsed();
	FReply OnCheckProject();

	TArray<CKSessions::Hub::FEntityType> Types;
	TArray<TSharedPtr<FCKSessionsHubEntry>> Entries;
	TArray<TSharedPtr<FCKSessionsHubEntry>> Visible;
	FString Category;
	FString SearchText;
	FString NewName;
	FText Report;
	FText SelectedTitle;
	FText SelectedDetails;
	TSharedPtr<SListView<TSharedPtr<FCKSessionsHubEntry>>> List;
	TSharedPtr<SSearchBox> SearchBox;
	TSharedPtr<SEditableTextBox> NameBox;
	TSharedPtr<SVerticalBox> CategoryBox;
	TSharedPtr<SWidgetSwitcher> LeftSwitcher;
};
