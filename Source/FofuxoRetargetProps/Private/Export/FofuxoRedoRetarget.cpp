// Fofuxo -- redoing the retarget of whatever was already exported

#include "FofuxoRedoRetarget.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "RetargetEditor/IKRetargetBatchOperation.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"
#include "SPositiveActionButton.h"
#include "Styling/AppStyle.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoRedo, Log, All);

namespace FofuxoRedo
{
	/**
	 * The editor behind a weak toolkit pointer.
	 *
	 * Confirming the name is not paranoia: the button lives inside the tab, and
	 * the tab survives the editor's mode changes.
	 */
	static FIKRetargetEditor* EditorOfToolkit(const TWeakPtr<FAssetEditorToolkit>& Weak)
	{
		const TSharedPtr<FAssetEditorToolkit> Toolkit = Weak.Pin();
		if (!Toolkit.IsValid() || Toolkit->GetEditorName() != FName("IKRetargetEditor"))
		{
			return nullptr;
		}

		return static_cast<FIKRetargetEditor*>(Toolkit.Get());
	}

	/** The first widget of this type, walking down the tree. */
	static TSharedPtr<SWidget> Search(const TSharedRef<SWidget>& Root, const FName Type)
	{
		if (Root->GetType() == Type)
		{
			return Root;
		}

		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
		{
			if (const TSharedPtr<SWidget> Found = Search(Children->GetChildAt(Index), Type))
			{
				return Found;
			}
		}

		return nullptr;
	}

	/**
	 * The Asset Browser tab's column -- the one with Export Selected Animations at
	 * the top, the list in the middle and Play Ref Pose at the bottom.
	 */
	static TSharedPtr<SVerticalBox> AssetBrowserColumn(FIKRetargetEditor& Editor)
	{
		const TSharedPtr<FTabManager> Tabs = Editor.GetTabManager();
		if (!Tabs.IsValid())
		{
			return nullptr;
		}

		// The value of FIKRetargetAssetBrowserTabSummoner::TabID. The struct has no
		// API macro, so the symbol doesn't link from outside IKRigEditor and the
		// name is spelled out here.
		const TSharedPtr<SDockTab> Tab = Tabs->FindExistingLiveTab(FTabId(TEXT("AssetBrowser")));
		if (!Tab.IsValid())
		{
			return nullptr;
		}

		const TSharedPtr<SWidget> Browser = Search(Tab->GetContent(), TEXT("SIKRetargetAssetBrowser"));
		if (!Browser.IsValid())
		{
			return nullptr;
		}

		const TSharedPtr<SWidget> Column = Search(Browser.ToSharedRef(), TEXT("SVerticalBox"));
		if (!Column.IsValid())
		{
			return nullptr;
		}

		return StaticCastSharedPtr<SVerticalBox>(Column);
	}

	/** A button already placed, so as not to repeat and to know what to take back. */
	struct FPlaced
	{
		TWeakPtr<SVerticalBox> Column;
		TWeakPtr<SWidget> Button;
	};

	static TArray<FPlaced> Placed;

	/** The three pieces of the retarget the batch needs. */
	struct FSides
	{
		USkeletalMesh* SourceMesh = nullptr;
		USkeletalMesh* TargetMesh = nullptr;
		UIKRetargeter* Retargeter = nullptr;

		bool Serve() const
		{
			return SourceMesh != nullptr
				&& TargetMesh != nullptr
				&& Retargeter != nullptr
				&& SourceMesh->GetSkeleton() != nullptr
				&& TargetMesh->GetSkeleton() != nullptr
				&& SourceMesh->GetSkeleton() != TargetMesh->GetSkeleton();
		}
	};

	static FSides SidesOfEditor(FIKRetargetEditor& Editor)
	{
		const TSharedRef<FIKRetargetEditorController> Controller = Editor.GetController();

		FSides Sides;

		// Through the AssetController, and not through
		// FIKRetargetEditorController::GetSkeletalMesh, which does exactly this one
		// line further along: that method has no IKRIGEDITOR_API and doesn't link
		// from outside the module.
		if (UIKRetargeterController* Owner = Controller->AssetController)
		{
			Sides.SourceMesh = Owner->GetPreviewMesh(ERetargetSourceOrTarget::Source);
			Sides.TargetMesh = Owner->GetPreviewMesh(ERetargetSourceOrTarget::Target);
			Sides.Retargeter = Owner->GetAsset();
		}

		return Sides;
	}

	/**
	 * The already-exported animations, grouped by the folder they live in -- and
	 * what goes in each group is each one's *source*, which is what the batch
	 * retarget eats.
	 *
	 * A single pass over the asset registry, loading nothing: an AnimSequence's
	 * skeleton is in the "Skeleton" tag, and the name is in the FAssetData itself.
	 */
	static void GatherByFolder(
		const FSides& Sides,
		TMap<FString, TArray<FAssetData>>& OutByFolder,
		TArray<FString>& OutNoSource)
	{
		IAssetRegistry& Registry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		TArray<FAssetData> All;
		Registry.GetAssetsByClass(UAnimSequence::StaticClass()->GetClassPathName(), All, /*bSearchSubClasses*/ true);

		const FString SourceSkeleton = FAssetData(Sides.SourceMesh->GetSkeleton()).GetExportTextName();
		const FString TargetSkeleton = FAssetData(Sides.TargetMesh->GetSkeleton()).GetExportTextName();

		TMap<FString, FAssetData> Sources;
		TArray<FAssetData> Exported;

		for (const FAssetData& Asset : All)
		{
			const FString Skeleton = Asset.GetTagValueRef<FString>(TEXT("Skeleton"));
			const FString Name = Asset.AssetName.ToString();

			if (Skeleton == SourceSkeleton)
			{
				// Two namesake sources in different folders: the first one stays,
				// and the other shows up in the log -- guessing which of the two
				// produced the file cannot be done, and choosing silently would be
				// worse.
				if (const FAssetData* Before = Sources.Find(Name))
				{
					UE_LOG(LogFofuxoRedo, Warning,
						TEXT("Two source animations called \"%s\": keeping %s and ignoring %s."),
						*Name, *Before->PackageName.ToString(), *Asset.PackageName.ToString());
					continue;
				}

				Sources.Add(Name, Asset);
			}
			else if (Skeleton == TargetSkeleton)
			{
				Exported.Add(Asset);
			}
		}

		for (const FAssetData& One : Exported)
		{
			const FString Name = One.AssetName.ToString();

			if (const FAssetData* Source = Sources.Find(Name))
			{
				const FString Folder = FPaths::GetPath(One.PackageName.ToString());
				OutByFolder.FindOrAdd(Folder).Add(*Source);
			}
			else
			{
				// A target-skeleton animation with no namesake on the source: made
				// by hand, or exported with a prefix or suffix. Not ours, leave it
				// alone.
				OutNoSource.Add(Name);
			}
		}
	}

	static FText BuildQuestion(
		const TMap<FString, TArray<FAssetData>>& ByFolder,
		const TArray<FString>& NoSource,
		const FSides& Sides)
	{
		int32 HowMany = 0;
		FString Folders;

		for (const TPair<FString, TArray<FAssetData>>& Pair : ByFolder)
		{
			HowMany += Pair.Value.Num();
			Folders += FString::Printf(TEXT("\n    %s  (%d)"), *Pair.Key, Pair.Value.Num());
		}

		FText Text = FText::Format(
			LOCTEXT("Question",
				"I found {0} animations that have already come out of this retarget:\n{1}\n\n"
				"I am going to redo them all with {2} and overwrite the assets that are there. Carry on?"),
			FText::AsNumber(HowMany),
			FText::FromString(Folders),
			FText::FromString(Sides.Retargeter->GetName()));

		if (NoSource.Num() > 0)
		{
			Text = FText::Format(
				LOCTEXT("QuestionWithLeftovers",
					"{0}\n\n({1} animations of {2}'s skeleton have no source of the same name and stay as "
					"they are. The names are in the Output Log.)"),
				Text,
				FText::AsNumber(NoSource.Num()),
				FText::FromString(Sides.TargetMesh->GetName()));
		}

		return Text;
	}
}

void FFofuxoRedoRetarget::EnsureButton(FIKRetargetEditor& Editor)
{
	const TSharedPtr<SVerticalBox> Column = FofuxoRedo::AssetBrowserColumn(Editor);
	if (!Column.IsValid())
	{
		// The tab is closed, or the editor is still building itself. It comes back
		// on the next tick.
		return;
	}

	FofuxoRedo::Placed.RemoveAll([](const FofuxoRedo::FPlaced& One)
	{
		return !One.Column.IsValid() || !One.Button.IsValid();
	});

	for (const FofuxoRedo::FPlaced& One : FofuxoRedo::Placed)
	{
		if (One.Column.Pin() == Column)
		{
			return;
		}
	}

	// Only the toolkit is kept: the concrete editor comes out of it at click time,
	// and a raw pointer would outlive the window's closing.
	const TWeakPtr<FAssetEditorToolkit> Weak = StaticCastSharedRef<FAssetEditorToolkit>(Editor.AsShared());

	const TSharedRef<SWidget> Button = SNew(SPositiveActionButton)
		.Icon(FAppStyle::Get().GetBrush("Icons.Refresh"))
		.Text(LOCTEXT("Redo", "Redo the already-exported ones"))
		.ToolTipText(LOCTEXT("RedoTip",
			"The same as the button above, only picking the animations by itself: the ones this "
			"retarget has already produced once. It is for after touching the retargeter, without "
			"having to find and tick them all in the list again.\n\n"
			"It recognizes them by name -- every animation of the target skeleton that has a namesake "
			"on the source skeleton came out of here. They are redone over the top, each one in the "
			"folder it already sits in."))
		.IsEnabled(TAttribute<bool>::CreateLambda([Weak]()
		{
			FIKRetargetEditor* Open = FofuxoRedo::EditorOfToolkit(Weak);
			return Open != nullptr && FofuxoRedo::SidesOfEditor(*Open).Serve();
		}))
		.OnClicked(FOnClicked::CreateLambda([Weak]()
		{
			if (FIKRetargetEditor* Open = FofuxoRedo::EditorOfToolkit(Weak))
			{
				FFofuxoRedoRetarget::OnClicked(*Open);
			}

			return FReply::Handled();
		}));

	// Slot 1: right under Export Selected Animations, before the list.
	Column->InsertSlot(1)
		.AutoHeight()
		[
			Button
		];

	FofuxoRedo::FPlaced New;
	New.Column = Column;
	New.Button = Button;
	FofuxoRedo::Placed.Add(New);
}

void FFofuxoRedoRetarget::Forget()
{
	for (const FofuxoRedo::FPlaced& One : FofuxoRedo::Placed)
	{
		const TSharedPtr<SVerticalBox> Column = One.Column.Pin();
		const TSharedPtr<SWidget> Button = One.Button.Pin();

		if (Column.IsValid() && Button.IsValid())
		{
			Column->RemoveSlot(Button.ToSharedRef());
		}
	}

	FofuxoRedo::Placed.Reset();
}

void FFofuxoRedoRetarget::OnClicked(FIKRetargetEditor& Editor)
{
	const FofuxoRedo::FSides Sides = FofuxoRedo::SidesOfEditor(Editor);
	if (!Sides.Serve())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoSides", "This retargeter needs a mesh on the source and another on the target, from different skeletons."));
		return;
	}

	TMap<FString, TArray<FAssetData>> ByFolder;
	TArray<FString> NoSource;
	FofuxoRedo::GatherByFolder(Sides, ByFolder, NoSource);

	for (const FString& Name : NoSource)
	{
		UE_LOG(LogFofuxoRedo, Display,
			TEXT("\"%s\" belongs to the target skeleton but has no source of the same name -- out of the redo."), *Name);
	}

	if (ByFolder.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("NothingToRedo",
				"I found no animation on {0}'s skeleton that has a source of the same name on {1}'s skeleton.\n\n"
				"That is what this button looks for: the batch retarget duplicates keeping the name, so that is how "
				"what has been through here can be recognized. If you exported with a prefix or a suffix, the name "
				"no longer matches."),
			FText::FromString(Sides.TargetMesh->GetName()),
			FText::FromString(Sides.SourceMesh->GetName())));

		return;
	}

	const EAppReturnType::Type Answer = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FofuxoRedo::BuildQuestion(ByFolder, NoSource, Sides),
		LOCTEXT("RedoTitle", "Redo the already-exported ones"));

	if (Answer != EAppReturnType::Yes)
	{
		return;
	}

	// One run per folder: the batch writes everything into a single destination, so
	// animations living in different folders have to go in separate rounds for each
	// to come back to where it came from.
	int32 Redone = 0;

	for (const TPair<FString, TArray<FAssetData>>& Pair : ByFolder)
	{
		FIKRetargetBatchOperationInputs Input;
		Input.AssetsToRetarget = Pair.Value;
		Input.SourceMesh = Sides.SourceMesh;
		Input.TargetMesh = Sides.TargetMesh;
		Input.IKRetargetAsset = Sides.Retargeter;
		Input.InOverrideSetNames = Sides.Retargeter->GetOverrideSetsToApply();
		Input.TargetPath = Pair.Key;
		Input.bUseSourcePath = false;

		// The same name as what is already there, and over the top: that is exactly
		// the point. Without this the batch would create AS_Thing1, AS_Thing2, and
		// whoever uses the animation would still be pointing at the wrong version.
		Input.bOverwriteExistingFiles = true;

		// Only what was asked for. Pulling in the referenced assets would bring
		// montages and blendspaces nobody exported from here.
		Input.bIncludeReferencedAssets = false;

		UE_LOG(LogFofuxoRedo, Display,
			TEXT("Redoing %d animations in %s"), Pair.Value.Num(), *Pair.Key);

		Redone += UIKRetargetBatchOperation::RunBatchRetarget(Input).Num();
	}

	UE_LOG(LogFofuxoRedo, Display, TEXT("The redo finished: %d animations."), Redone);
}

#undef LOCTEXT_NAMESPACE
