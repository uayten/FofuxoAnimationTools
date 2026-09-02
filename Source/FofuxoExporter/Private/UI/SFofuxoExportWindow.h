// Fofuxo

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class ITableRow;
class SBorder;
class SExpandableArea;
class STableViewBase;
class SWindow;
class UAnimSequence;
class UFofuxoExportOptions;

/** A row of the animation list: the animation, and whether it goes. */
struct FFofuxoAnimationItem
{
	TWeakObjectPtr<UAnimSequence> Animation;
	bool bExport = true;
};

/**
 * The Fofuxo export window: the options in a details panel, the animation list
 * with checkboxes underneath, and the buttons.
 *
 * The list is an SListView and not a TArray in the details panel because with a
 * hundred animations one row per struct becomes impossible to tick.
 *
 * The widget is only the widget. Who gathers the animations, opens it and runs
 * the export is FofuxoExportFlow.
 */
class SFofuxoExportWindow : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SFofuxoExportWindow) {}
		SLATE_ARGUMENT(TSharedPtr<SWindow>, Window)
		SLATE_ARGUMENT(UFofuxoExportOptions*, Options)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/**
	 * Fills the list. Whatever is in `Unticked` -- asset paths -- comes in
	 * unticked; anything the set doesn't know comes in ticked, which is the
	 * useful default for an animation that wasn't there last time.
	 */
	void SetAnimations(const TArray<UAnimSequence*>& Animations, const TSet<FString>& Unticked);

	/** The ticked animations, in list order -- including the ones the filter hides. */
	TArray<UAnimSequence*> Ticked() const;

	/**
	 * Folds this window's tick state into the set of unticked paths, touching
	 * only the animations that were on this list -- the ones belonging to other
	 * characters have to survive.
	 */
	void CollectTickState(TSet<FString>& InOutUnticked) const;

	/** Whether the export button was the one that closed the window. */
	bool Confirmed() const { return bConfirmed; }

	bool IsListExpanded() const;

	float GetListHeight() const { return ListHeight; }

private:

	TSharedRef<ITableRow> GenerateRow(TSharedPtr<FFofuxoAnimationItem> Item, const TSharedRef<STableViewBase>& Table);
	FReply TickAll(bool bTick);
	FReply OnExport();
	FReply OnCancel();
	bool CanExport() const;
	FText SummaryText() const;

	/** Rebuilds the visible list from the search text. */
	void ApplyFilter();
	void OnSearchChanged(const FText& Text);

	// The drag handle under the list. While the button is held, every move
	// becomes height; the clamp is so the list neither vanishes nor pushes the
	// buttons out of the window.
	FReply OnGrabHandle(const FGeometry& Geometry, const FPointerEvent& Event);
	FReply OnDragHandle(const FGeometry& Geometry, const FPointerEvent& Event);
	FReply OnReleaseHandle(const FGeometry& Geometry, const FPointerEvent& Event);

	TWeakPtr<SWindow> Window;
	UFofuxoExportOptions* Options = nullptr;

	/** Every animation. This is where what goes into the file comes from. */
	TArray<TSharedPtr<FFofuxoAnimationItem>> Items;

	/** The ones that pass the filter. What the list shows and what the buttons tick. */
	TArray<TSharedPtr<FFofuxoAnimationItem>> Visible;

	FString Search;

	TSharedPtr<SListView<TSharedPtr<FFofuxoAnimationItem>>> List;
	TSharedPtr<SExpandableArea> ListArea;
	TSharedPtr<SBorder> Handle;

	float ListHeight = 300.f;
	float HeightOnGrab = 0.f;
	float MouseOnGrab = 0.f;

	bool bConfirmed = false;
};
