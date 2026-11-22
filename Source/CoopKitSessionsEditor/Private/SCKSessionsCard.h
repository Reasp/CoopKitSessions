// Copyright 2026 Ivan Iaitckikh. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "CKSessionsWorkspace.h"

class FAssetThumbnailPool;
class ITableRow;
class STableViewBase;
struct FPropertyChangedEvent;

/**
 * Parts tab of the card: every part of the entity in other assets (the CoopKitPart properties) with a thumbnail, an own /
 * shared label and Open / Replace / Create, plus the kit's warnings (assets in the entity's folder other assets use).
 */
class COOPKITSESSIONSEDITOR_API SCKSessionsParts final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCKSessionsParts) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UObject* InEntity);
	virtual ~SCKSessionsParts() override;

	/** Reads the parts again (after an edit of the entity). */
	void Refresh();

	/** Parts and empty slots shown, for tests. */
	const TArray<TSharedPtr<CKSessions::Workspace::FCKSessionsEntityPart>>& GetItems() const
	{
		return Items;
	}

private:
	TSharedRef<ITableRow> MakeRow(TSharedPtr<CKSessions::Workspace::FCKSessionsEntityPart> Part, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<SWidget> MakeReplaceMenu(TSharedPtr<CKSessions::Workspace::FCKSessionsEntityPart> Part);
	void OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);

	TWeakObjectPtr<UObject> Entity;
	TArray<TSharedPtr<CKSessions::Workspace::FCKSessionsEntityPart>> Items;
	TSharedPtr<SListView<TSharedPtr<CKSessions::Workspace::FCKSessionsEntityPart>>> List;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	FText KitWarnings;
	FDelegateHandle PropertyChangedHandle;
};

/**
 * Preview tab of the card: the entity's name and a large thumbnail of its first part that is set. Entities with a look of
 * their own (a mesh, a widget) replace it with a viewport in the plugin.
 */
class COOPKITSESSIONSEDITOR_API SCKSessionsPreview final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCKSessionsPreview) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UObject* InEntity);
	virtual ~SCKSessionsPreview() override;

	/** The asset the thumbnail shows now; invalid when no part is set. For tests. */
	const FAssetData& GetPreviewedAsset() const
	{
		return Previewed;
	}

private:
	void Rebuild();
	void OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);

	TWeakObjectPtr<UObject> Entity;
	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	FAssetData Previewed;
	FDelegateHandle PropertyChangedHandle;
};
