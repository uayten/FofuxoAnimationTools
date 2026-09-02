// Fofuxo -- copying the retarget pose from another retargeter

#include "FofuxoCopyPose.h"

#include "FofuxoPoseOnDisk.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "ReferenceSkeleton.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "ToolMenu.h"
#include "ToolMenuContext.h"
#include "ToolMenuSection.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoCopyPose, Log, All);

namespace FofuxoCopy
{
	/** How far a delta may be from identity and still count as unposed. */
	static constexpr float Slack = 1.0e-6f;

	static const ERetargetSourceOrTarget Sides[] =
	{
		ERetargetSourceOrTarget::Source,
		ERetargetSourceOrTarget::Target,
	};

	static FText NameOfSide(const ERetargetSourceOrTarget Side)
	{
		return Side == ERetargetSourceOrTarget::Source
			? LOCTEXT("SideSource", "Source")
			: LOCTEXT("SideTarget", "Target");
	}

	/** Where the pose is going: the side the editor is editing right now. */
	struct FDestination
	{
		UIKRetargeterController* AssetController = nullptr;
		UIKRetargeter* Asset = nullptr;
		USkeletalMesh* Mesh = nullptr;
		ERetargetSourceOrTarget Side = ERetargetSourceOrTarget::Source;
		FName Pose;

		bool Serves() const { return AssetController != nullptr && Asset != nullptr && Mesh != nullptr; }
	};

	static FDestination DestinationOfEditor(FIKRetargetEditor& Editor)
	{
		const TSharedRef<FIKRetargetEditorController> Controller = Editor.GetController();

		FDestination Destination;
		Destination.AssetController = Controller->AssetController;
		Destination.Side = Controller->GetSourceOrTarget();

		if (Destination.AssetController != nullptr)
		{
			Destination.Asset = Destination.AssetController->GetAsset();
			Destination.Mesh = Destination.AssetController->GetPreviewMesh(Destination.Side);
			Destination.Pose = Destination.AssetController->GetCurrentRetargetPoseName(Destination.Side);
		}

		return Destination;
	}

	/** The project's retargeters, alphabetically, without loading any of them. */
	static void Retargeters(TArray<FAssetData>& OutAssets)
	{
		IAssetRegistry& Registry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		Registry.GetAssetsByClass(UIKRetargeter::StaticClass()->GetClassPathName(), OutAssets, /*bSearchSubClasses*/ true);

		OutAssets.Sort([](const FAssetData& A, const FAssetData& B)
		{
			return A.AssetName.LexicalLess(B.AssetName);
		});
	}

	/** How many bones of this pose are actually posed. */
	static int32 HowManyPosed(const FIKRetargetPose& Pose)
	{
		int32 HowMany = 0;

		for (const TTuple<FName, FQuat>& Pair : Pose.GetAllDeltaRotations())
		{
			if (!Pair.Value.Equals(FQuat::Identity, Slack))
			{
				++HowMany;
			}
		}

		return HowMany;
	}

	/** What the copy is going to do, counted before anything is written. */
	struct FTally
	{
		// Bone and delta, only those that exist in the destination's skeleton.
		TArray<TTuple<FName, FQuat>> Matching;

		// Posed in the source, and this skeleton doesn't have them.
		TArray<FName> NoSuchBone;

		// Posed here and not over there, which the copy returns to the ref pose.
		TArray<FName> Cleared;

		FVector Pelvis = FVector::ZeroVector;
	};

	static FTally Check(
		const FIKRetargetPose& Source,
		const FIKRetargetPose& Current,
		const FReferenceSkeleton& Skeleton)
	{
		FTally Tally;
		Tally.Pelvis = Source.GetRootTranslationDelta();

		TSet<FName> Incoming;

		for (const TTuple<FName, FQuat>& Pair : Source.GetAllDeltaRotations())
		{
			if (Pair.Value.Equals(FQuat::Identity, Slack))
			{
				// A bone the source lists but did not pose. Copying identity would
				// only fill the destination's map with entries that do nothing.
				continue;
			}

			if (Skeleton.FindBoneIndex(Pair.Key) == INDEX_NONE)
			{
				Tally.NoSuchBone.Add(Pair.Key);
				continue;
			}

			Tally.Matching.Emplace(Pair.Key, Pair.Value);
			Incoming.Add(Pair.Key);
		}

		for (const TTuple<FName, FQuat>& Pair : Current.GetAllDeltaRotations())
		{
			if (!Pair.Value.Equals(FQuat::Identity, Slack) && !Incoming.Contains(Pair.Key))
			{
				Tally.Cleared.Add(Pair.Key);
			}
		}

		Tally.Matching.Sort([](const TTuple<FName, FQuat>& A, const TTuple<FName, FQuat>& B)
		{
			return A.Key.LexicalLess(B.Key);
		});

		Tally.NoSuchBone.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		Tally.Cleared.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });

		return Tally;
	}

	static FText BuildQuestion(
		const FTally& Tally,
		const FDestination& Destination,
		const UIKRetargeter& Source,
		const ERetargetSourceOrTarget SourceSide,
		const FName SourcePose)
	{
		FText Text = FText::Format(
			LOCTEXT("CopyQuestion",
				"Copy the pose \"{0}\" from the {1} side of {2} into the pose \"{3}\" of the {4} side of {5}?\n\n"
				"{6} bones posed over there exist in this skeleton and come across by name."),
			FText::FromName(SourcePose),
			NameOfSide(SourceSide),
			FText::FromString(Source.GetName()),
			FText::FromName(Destination.Pose),
			NameOfSide(Destination.Side),
			FText::FromString(Destination.Asset->GetName()),
			FText::AsNumber(Tally.Matching.Num()));

		if (Tally.NoSuchBone.Num() > 0)
		{
			Text = FText::Format(
				LOCTEXT("CopyNoSuchBone",
					"{0}\n{1} do not exist in {2} and are left out (the names go to the Output Log)."),
				Text,
				FText::AsNumber(Tally.NoSuchBone.Num()),
				FText::FromString(Destination.Mesh->GetName()));
		}

		if (Tally.Cleared.Num() > 0)
		{
			Text = FText::Format(
				LOCTEXT("CopyCleared",
					"{0}\n{1} bones you posed here are not posed over there and go back to the ref pose "
					"-- copying is ending up alike, not adding up."),
				Text,
				FText::AsNumber(Tally.Cleared.Num()));
		}

		if (!Tally.Pelvis.IsNearlyZero())
		{
			Text = FText::Format(
				LOCTEXT("CopyPelvis",
					"{0}\n\nThe pelvis offset comes along ({1}). That is the only value in centimetres in "
					"this copy: between characters of different sizes, check it afterwards."),
				Text,
				FText::FromString(Tally.Pelvis.ToCompactString()));
		}

		return FText::Format(LOCTEXT("CopyClosing", "{0}\n\nOne Ctrl+Z undoes all of it at once."), Text);
	}
}

FIKRetargetEditor* FFofuxoCopyPose::EditorOfContext(const FToolMenuContext& Context)
{
	const UAssetEditorToolkitMenuContext* FromEditor = Context.FindContext<UAssetEditorToolkitMenuContext>();
	if (FromEditor == nullptr)
	{
		return nullptr;
	}

	const TSharedPtr<FAssetEditorToolkit> Toolkit = FromEditor->Toolkit.Pin();
	if (!Toolkit.IsValid() || Toolkit->GetEditorName() != FName("IKRetargetEditor"))
	{
		return nullptr;
	}

	return static_cast<FIKRetargetEditor*>(Toolkit.Get());
}

bool FFofuxoCopyPose::Can(const FToolMenuContext& Context)
{
	FIKRetargetEditor* Editor = EditorOfContext(Context);

	return Editor != nullptr && FofuxoCopy::DestinationOfEditor(*Editor).Serves();
}

void FFofuxoCopyPose::BuildMenu(UToolMenu* Menu)
{
	FIKRetargetEditor* Editor = EditorOfContext(Menu->Context);
	if (Editor == nullptr)
	{
		return;
	}

	const FofuxoCopy::FDestination Destination = FofuxoCopy::DestinationOfEditor(*Editor);
	if (!Destination.Serves())
	{
		return;
	}

	// The disk section comes first: the retargeter list grows with the project,
	// and the file shouldn't end up at the bottom of a scroll.
	FFofuxoPoseOnDisk::BuildSection(Menu);

	// The section's header says where the pose is going. Without it the menu
	// doesn't show which of the two sides you are pasting into, and the
	// Source/Target button is far away.
	FToolMenuSection& Section = Menu->FindOrAddSection("FofuxoCopyPose", FText::Format(
		LOCTEXT("CopyInto", "Paste into: {0} of {1}, pose \"{2}\""),
		FofuxoCopy::NameOfSide(Destination.Side),
		FText::FromString(Destination.Asset->GetName()),
		FText::FromName(Destination.Pose)));

	TArray<FAssetData> Assets;
	FofuxoCopy::Retargeters(Assets);

	const FSoftObjectPath DestinationPath(Destination.Asset);

	for (const FAssetData& Asset : Assets)
	{
		const FSoftObjectPath Path = Asset.GetSoftObjectPath();
		const bool bIsThisOne = Path == DestinationPath;

		Section.AddSubMenu(
			Asset.AssetName,
			bIsThisOne
				? FText::Format(LOCTEXT("CopyThisOne", "{0}  (this one)"), FText::FromName(Asset.AssetName))
				: FText::FromName(Asset.AssetName),
			FText::FromString(Asset.PackageName.ToString()),
			FNewToolMenuChoice(FNewToolMenuDelegate::CreateLambda([Path](UToolMenu* Submenu)
			{
				// Only now is the asset loaded -- listing the poses requires
				// opening the file, and opening every retargeter in the project to
				// draw a menu would be expensive for nothing.
				BuildSubmenuForOneRetargeter(Submenu, Path);
			})));
	}
}

void FFofuxoCopyPose::BuildSubmenuForOneRetargeter(UToolMenu* Menu, const FSoftObjectPath& Path)
{
	FIKRetargetEditor* Editor = EditorOfContext(Menu->Context);
	if (Editor == nullptr)
	{
		return;
	}

	const FofuxoCopy::FDestination Destination = FofuxoCopy::DestinationOfEditor(*Editor);

	UIKRetargeter* Source = Cast<UIKRetargeter>(Path.TryLoad());
	if (Source == nullptr)
	{
		return;
	}

	UIKRetargeterController* FromSource = UIKRetargeterController::GetController(Source);
	if (FromSource == nullptr)
	{
		return;
	}

	const bool bSameAsset = Source == Destination.Asset;

	for (const ERetargetSourceOrTarget Side : FofuxoCopy::Sides)
	{
		const USkeletalMesh* Mesh = FromSource->GetPreviewMesh(Side);

		FToolMenuSection& Section = Menu->FindOrAddSection(
			Side == ERetargetSourceOrTarget::Source ? "Source" : "Target",
			Mesh != nullptr
				? FText::Format(LOCTEXT("CopySideWithMesh", "{0} -- {1}"),
					FofuxoCopy::NameOfSide(Side), FText::FromString(Mesh->GetName()))
				: FofuxoCopy::NameOfSide(Side));

		TArray<FName> Names;
		FromSource->GetRetargetPoses(Side).GenerateKeyArray(Names);
		Names.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });

		for (const FName& Name : Names)
		{
			// The very pose being edited is the source of nothing.
			if (bSameAsset && Side == Destination.Side && Name == Destination.Pose)
			{
				continue;
			}

			const FIKRetargetPose& Pose = FromSource->GetRetargetPoses(Side)[Name];
			const int32 Posed = FofuxoCopy::HowManyPosed(Pose);

			Section.AddMenuEntry(
				*FString::Printf(TEXT("%s_%s"),
					Side == ERetargetSourceOrTarget::Source ? TEXT("Source") : TEXT("Target"), *Name.ToString()),
				FText::FromName(Name),
				FText::Format(
					LOCTEXT("CopyPoseTip", "{0} posed bones. The pose here is replaced by this one."),
					FText::AsNumber(Posed)),
				FSlateIcon(),
				FToolUIActionChoice(FToolUIAction(
					FToolMenuExecuteAction::CreateLambda(
						[Path, Side, Name](const FToolMenuContext& Context)
						{
							FFofuxoCopyPose::Apply(Context, Path, Side, Name);
						}))));
		}
	}
}

void FFofuxoCopyPose::Apply(
	const FToolMenuContext& Context,
	const FSoftObjectPath Path,
	const ERetargetSourceOrTarget SourceSide,
	const FName SourcePose)
{
	FIKRetargetEditor* Editor = EditorOfContext(Context);
	if (Editor == nullptr)
	{
		return;
	}

	const FofuxoCopy::FDestination Destination = FofuxoCopy::DestinationOfEditor(*Editor);
	if (!Destination.Serves())
	{
		return;
	}

	UIKRetargeter* Source = Cast<UIKRetargeter>(Path.TryLoad());
	if (Source == nullptr)
	{
		return;
	}

	const FIKRetargetPose* Pose = Source->GetRetargetPoseByName(SourceSide, SourcePose);
	if (Pose == nullptr)
	{
		return;
	}

	const FIKRetargetPose& Current = Destination.AssetController->GetCurrentRetargetPose(Destination.Side);

	const FofuxoCopy::FTally Tally =
		FofuxoCopy::Check(*Pose, Current, Destination.Mesh->GetRefSkeleton());

	if (Tally.Matching.IsEmpty() && Tally.Cleared.IsEmpty() && Tally.Pelvis.IsNearlyZero())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("CopyNothing",
				"The pose \"{0}\" of {1} has no posed bone that exists in {2}, and the pose here is "
				"already cleared. There is nothing to copy."),
			FText::FromName(SourcePose),
			FText::FromString(Source->GetName()),
			FText::FromString(Destination.Mesh->GetName())));

		return;
	}

	for (const FName& Bone : Tally.NoSuchBone)
	{
		UE_LOG(LogFofuxoCopyPose, Display,
			TEXT("\"%s\" is posed in %s but does not exist in %s -- out of the copy."),
			*Bone.ToString(), *Source->GetName(), *Destination.Mesh->GetName());
	}

	const EAppReturnType::Type Answer = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FofuxoCopy::BuildQuestion(Tally, Destination, *Source, SourceSide, SourcePose),
		LOCTEXT("CopyTitle", "Copy retarget pose"));

	if (Answer != EAppReturnType::Yes)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("CopyTransaction", "Copy retarget pose"));

	Destination.Asset->Modify();

	// A single reinitialization, at the end of the scope. Without it the
	// retargeter rebuilds itself once per bone, and a whole pose is ninety-odd.
	const FScopedReinitializeIKRetargeter Reinitialize(Destination.AssetController);

	// Clear before writing: whatever was left here and didn't come from over there
	// would make the destination's pose the sum of the two, and not a copy of one.
	Destination.AssetController->ResetRetargetPose(Destination.Pose, TArray<FName>(), Destination.Side);

	for (const TTuple<FName, FQuat>& Pair : Tally.Matching)
	{
		Destination.AssetController->SetRotationOffsetForRetargetPoseBone(Pair.Key, Pair.Value, Destination.Side);
	}

	if (!Tally.Pelvis.IsNearlyZero())
	{
		// The Reset has just left the offset at zero, so adding is the same as
		// assigning -- and adding is the only verb the controller offers.
		Destination.AssetController->SetRootOffsetInRetargetPose(Tally.Pelvis, Destination.Side);
	}

	UE_LOG(LogFofuxoCopyPose, Display,
		TEXT("Copied the pose \"%s\" from the %s side of %s into \"%s\" of %s: %d bones, %d left out, %d cleared."),
		*SourcePose.ToString(),
		*FofuxoCopy::NameOfSide(SourceSide).ToString(),
		*Source->GetName(),
		*Destination.Pose.ToString(),
		*Destination.Asset->GetName(),
		Tally.Matching.Num(),
		Tally.NoSuchBone.Num(),
		Tally.Cleared.Num());
}

#undef LOCTEXT_NAMESPACE
