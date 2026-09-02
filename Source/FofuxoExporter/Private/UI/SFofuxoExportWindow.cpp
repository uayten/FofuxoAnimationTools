// Fofuxo

#include "SFofuxoExportWindow.h"

#include "FofuxoExportOptions.h"

#include "Animation/AnimSequence.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "FofuxoExporter"

void SFofuxoExportWindow::Construct(const FArguments& InArgs)
{
	Window = InArgs._Window;
	Options = InArgs._Options;

	if (Options != nullptr && Options->ListHeight > 0.f)
	{
		ListHeight = Options->ListHeight;
	}

	FPropertyEditorModule& PropertyEditor =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs Arguments;
	Arguments.bAllowSearch = false;
	Arguments.bShowOptions = false;
	Arguments.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	const TSharedRef<IDetailsView> Details = PropertyEditor.CreateDetailView(Arguments);
	Details->SetObject(Options);

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(4.f)
		[
			Details
		]

		// Collapsed, the list gives its height back to the details panel --
		// which with the list open is too cramped to see all of Advanced.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.f, 2.f, 8.f, 4.f)
		[
			SAssignNew(ListArea, SExpandableArea)
			.InitiallyCollapsed(Options == nullptr || !Options->bListExpanded)
			.HeaderContent()
			[
				SNew(STextBlock)
				.Text(this, &SFofuxoExportWindow::SummaryText)
			]
			.BodyContent()
			[
				SNew(SVerticalBox)

				// The handle sits on top because that is the edge that moves:
				// the Export buttons hold the bottom of the window, so the list
				// grows upwards, eating the details panel's height.
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 2.f)
				[
					SAssignNew(Handle, SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("Brushes.Header"))
					.Padding(0.f)
					.Cursor(EMouseCursor::ResizeUpDown)
					.ToolTipText(LOCTEXT("HandleTip", "Drag to change the list's height."))
					.OnMouseButtonDown(this, &SFofuxoExportWindow::OnGrabHandle)
					.OnMouseMove(this, &SFofuxoExportWindow::OnDragHandle)
					.OnMouseButtonUp(this, &SFofuxoExportWindow::OnReleaseHandle)
					[
						SNew(SBox)
						.HeightOverride(6.f)
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 2.f, 0.f, 4.f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(SSearchBox)
						.HintText(LOCTEXT("Search", "Search by name"))
						.ToolTipText(LOCTEXT("SearchTip",
							"Hides from the list whatever doesn't match the text. It is only the view: a "
							"ticked animation still goes into the file while hidden. Tick all and Untick "
							"all apply to whatever the search is showing."))
						.OnTextChanged(this, &SFofuxoExportWindow::OnSearchChanged)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("TickAll", "Tick all"))
						.OnClicked(this, &SFofuxoExportWindow::TickAll, true)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("UntickAll", "Untick all"))
						.OnClicked(this, &SFofuxoExportWindow::TickAll, false)
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					// An attribute, not a fixed value: that way dragging the
					// handle shows up on the same frame, rebuilding no widget.
					.HeightOverride(TAttribute<FOptionalSize>::CreateLambda([this]()
					{
						return FOptionalSize(ListHeight);
					}))
					[
						SAssignNew(List, SListView<TSharedPtr<FFofuxoAnimationItem>>)
						.ListItemsSource(&Visible)
						.SelectionMode(ESelectionMode::None)
						.OnGenerateRow(this, &SFofuxoExportWindow::GenerateRow)
					]
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.f, 4.f, 8.f, 8.f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Export", "Export"))
				.IsEnabled(TAttribute<bool>::CreateSP(this, &SFofuxoExportWindow::CanExport))
				.OnClicked(this, &SFofuxoExportWindow::OnExport)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Cancel", "Cancel"))
				.OnClicked(this, &SFofuxoExportWindow::OnCancel)
			]
		]
	];
}

void SFofuxoExportWindow::SetAnimations(const TArray<UAnimSequence*>& Animations, const TSet<FString>& Unticked)
{
	Items.Reset();
	Items.Reserve(Animations.Num());

	for (UAnimSequence* Sequence : Animations)
	{
		if (Sequence == nullptr)
		{
			continue;
		}

		TSharedPtr<FFofuxoAnimationItem> Item = MakeShared<FFofuxoAnimationItem>();
		Item->Animation = Sequence;
		Item->bExport = !Unticked.Contains(Sequence->GetPathName());
		Items.Add(Item);
	}

	ApplyFilter();
}

void SFofuxoExportWindow::CollectTickState(TSet<FString>& InOutUnticked) const
{
	for (const TSharedPtr<FFofuxoAnimationItem>& Item : Items)
	{
		if (!Item->Animation.IsValid())
		{
			continue;
		}

		const FString Path = Item->Animation->GetPathName();
		if (Item->bExport)
		{
			InOutUnticked.Remove(Path);
		}
		else
		{
			InOutUnticked.Add(Path);
		}
	}
}

bool SFofuxoExportWindow::IsListExpanded() const
{
	return ListArea.IsValid() ? ListArea->IsExpanded() : true;
}

TSharedRef<ITableRow> SFofuxoExportWindow::GenerateRow(
	TSharedPtr<FFofuxoAnimationItem> Item, const TSharedRef<STableViewBase>& Table)
{
	const FString Name = Item->Animation.IsValid() ? Item->Animation->GetName() : FString();

	return SNew(STableRow<TSharedPtr<FFofuxoAnimationItem>>, Table)
		.Padding(FMargin(4.f, 2.f))
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([Item]()
			{
				return Item->bExport ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([Item](ECheckBoxState State)
			{
				Item->bExport = (State == ECheckBoxState::Checked);
			})
			[
				SNew(STextBlock)
				.Margin(FMargin(4.f, 0.f, 0.f, 0.f))
				.Text(FText::FromString(Name))
			]
		];
}

FReply SFofuxoExportWindow::TickAll(bool bTick)
{
	// Visible, not Items: with a search active, "all" is what is in sight --
	// otherwise the button would silently undo ticks you cannot even see.
	for (const TSharedPtr<FFofuxoAnimationItem>& Item : Visible)
	{
		Item->bExport = bTick;
	}

	if (List.IsValid())
	{
		List->RebuildList();
	}

	return FReply::Handled();
}

TArray<UAnimSequence*> SFofuxoExportWindow::Ticked() const
{
	TArray<UAnimSequence*> Chosen;

	for (const TSharedPtr<FFofuxoAnimationItem>& Item : Items)
	{
		if (Item->bExport && Item->Animation.IsValid())
		{
			Chosen.Add(Item->Animation.Get());
		}
	}

	return Chosen;
}

FText SFofuxoExportWindow::SummaryText() const
{
	int32 TickedCount = 0;
	for (const TSharedPtr<FFofuxoAnimationItem>& Item : Items)
	{
		TickedCount += Item->bExport ? 1 : 0;
	}

	// The summary says how many files that will land in, and in the chosen
	// format. It used to always say "in a single FBX", which became a lie twice
	// over: with USD chosen, and with a batch smaller than the selection.
	FText Format = LOCTEXT("FormatFbx", "FBX");
	if (Options != nullptr && Options->Format == EFofuxoFormat::USD)
	{
		Format = LOCTEXT("FormatUsd", "USD");
	}
	else if (Options != nullptr && Options->Format == EFofuxoFormat::GLTF)
	{
		Format = LOCTEXT("FormatGltf", "glTF");
	}

	const bool bWithMesh = Options == nullptr || Options->bExportMesh;

	// None ticked means mesh only -- and then the animation and file counts say
	// nothing. Without the mesh as well, nothing is left: it is the only state
	// in which Export greys out with a mesh and a folder chosen, so the summary
	// is what explains why.
	if (TickedCount == 0)
	{
		return bWithMesh
			? FText::Format(
				LOCTEXT("SummaryMeshOnly", "No animation ticked: only the mesh comes out, in a single {0}"),
				Format)
			: LOCTEXT("SummaryNothing",
				"No animation ticked and the mesh turned off: there is nothing left to export");
	}

	const int32 PerFile = (Options != nullptr && Options->TakesPerFile > 0)
		? Options->TakesPerFile
		: TickedCount;

	const int32 NumFiles = (TickedCount > 0 && PerFile > 0)
		? FMath::DivideAndRoundUp(TickedCount, PerFile)
		: 1;

	const FText Where = NumFiles <= 1
		? FText::Format(LOCTEXT("InOneFile", "in a single {0}"), Format)
		: FText::Format(
			LOCTEXT("InSeveralFiles", "across {0} {1} files"),
			FText::AsNumber(NumFiles),
			Format);

	const FText Summary = bWithMesh
		? FText::Format(
			LOCTEXT("Summary", "{0} of {1} animations, {2}"),
			FText::AsNumber(TickedCount),
			FText::AsNumber(Items.Num()),
			Where)
		: FText::Format(
			LOCTEXT("SummaryNoMesh", "{0} of {1} animations, without the mesh, {2}"),
			FText::AsNumber(TickedCount),
			FText::AsNumber(Items.Num()),
			Where);

	if (Search.IsEmpty())
	{
		return Summary;
	}

	return FText::Format(
		LOCTEXT("SummaryFiltered", "{0} -- the search shows {1}"),
		Summary,
		FText::AsNumber(Visible.Num()));
}

void SFofuxoExportWindow::ApplyFilter()
{
	Visible.Reset();

	for (const TSharedPtr<FFofuxoAnimationItem>& Item : Items)
	{
		if (!Item->Animation.IsValid())
		{
			continue;
		}

		// Contains already ignores letter case.
		if (Search.IsEmpty() || Item->Animation->GetName().Contains(Search))
		{
			Visible.Add(Item);
		}
	}

	if (List.IsValid())
	{
		List->RequestListRefresh();
	}
}

void SFofuxoExportWindow::OnSearchChanged(const FText& Text)
{
	Search = Text.ToString().TrimStartAndEnd();
	ApplyFilter();
}

FReply SFofuxoExportWindow::OnGrabHandle(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!Handle.IsValid() || Event.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	HeightOnGrab = ListHeight;
	MouseOnGrab = static_cast<float>(Event.GetScreenSpacePosition().Y);

	// Without capturing the mouse, the pointer leaves the bar on the first
	// slightly faster move and the drag dies halfway through.
	return FReply::Handled().CaptureMouse(Handle.ToSharedRef());
}

FReply SFofuxoExportWindow::OnDragHandle(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!Handle.IsValid() || !Handle->HasMouseCapture())
	{
		return FReply::Unhandled();
	}

	const float Moved = static_cast<float>(Event.GetScreenSpacePosition().Y) - MouseOnGrab;

	// Subtracting: the handle is at the top, so dragging upwards -- Y going down
	// -- is what makes the list grow, and the bar stays under the finger.
	//
	// The ceiling is so the list doesn't push Export out of the window; the
	// floor, so there is always a visible row left.
	ListHeight = FMath::Clamp(HeightOnGrab - Moved, 80.f, 900.f);

	return FReply::Handled();
}

FReply SFofuxoExportWindow::OnReleaseHandle(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!Handle.IsValid() || !Handle->HasMouseCapture())
	{
		return FReply::Unhandled();
	}

	return FReply::Handled().ReleaseMouseCapture();
}

bool SFofuxoExportWindow::CanExport() const
{
	if (Options == nullptr || Options->SkeletalMesh.IsNull() || Options->Folder.Path.IsEmpty())
	{
		return false;
	}

	for (const TSharedPtr<FFofuxoAnimationItem>& Item : Items)
	{
		if (Item->bExport)
		{
			return true;
		}
	}

	// No animation ticked is still a valid request: the mesh comes out on its
	// own. What cannot be asked for is both turned off -- nothing would be left
	// in the file.
	return Options->bExportMesh;
}

FReply SFofuxoExportWindow::OnExport()
{
	bConfirmed = true;
	if (const TSharedPtr<SWindow> Pinned = Window.Pin())
	{
		Pinned->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SFofuxoExportWindow::OnCancel()
{
	bConfirmed = false;
	if (const TSharedPtr<SWindow> Pinned = Window.Pin())
	{
		Pinned->RequestDestroyWindow();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
