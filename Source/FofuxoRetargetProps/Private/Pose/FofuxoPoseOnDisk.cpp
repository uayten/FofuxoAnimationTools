// Fofuxo -- saving and applying the retarget pose as an asset

#include "FofuxoPoseOnDisk.h"

#include "FofuxoRetargetPose.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
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
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoPoseOnDisk, Log, All);

namespace FofuxoDisk
{
	/** How far a rotation may be from identity and still count as unposed. */
	static constexpr float Slack = 1.0e-6f;

	/** The side the editor is editing, with everything both ends need. */
	struct FSide
	{
		UIKRetargeterController* AssetController = nullptr;
		UIKRetargeter* Asset = nullptr;
		USkeletalMesh* Mesh = nullptr;
		ERetargetSourceOrTarget Which = ERetargetSourceOrTarget::Source;
		FName Pose;

		bool Serves() const { return AssetController != nullptr && Asset != nullptr && Mesh != nullptr; }
	};

	static FSide SideOfEditor(FIKRetargetEditor& Editor)
	{
		const TSharedRef<FIKRetargetEditorController> Controller = Editor.GetController();

		FSide Side;
		Side.AssetController = Controller->AssetController;
		Side.Which = Controller->GetSourceOrTarget();

		if (Side.AssetController != nullptr)
		{
			Side.Asset = Side.AssetController->GetAsset();
			Side.Mesh = Side.AssetController->GetPreviewMesh(Side.Which);
			Side.Pose = Side.AssetController->GetCurrentRetargetPoseName(Side.Which);
		}

		return Side;
	}

	static FText NameOfSide(const ERetargetSourceOrTarget Which)
	{
		return Which == ERetargetSourceOrTarget::Source
			? LOCTEXT("DiskSideSource", "Source")
			: LOCTEXT("DiskSideTarget", "Target");
	}

	/** What applying will do, counted before anything is written. */
	struct FTally
	{
		/** Bone and delta, already converted to *this* skeleton's ref pose. */
		TArray<TTuple<FName, FQuat>> Matching;

		/** Bones from the asset that this skeleton doesn't have. */
		TArray<FName> NoSuchBone;

		/** Bones from the asset that are already where they should be -- no delta. */
		int32 AlreadyThere = 0;

		/** Bones of this skeleton the asset says nothing about. */
		int32 Unmentioned = 0;

		/** Bones posed here that applying returns to the ref pose. */
		int32 Cleared = 0;

		FVector Pelvis = FVector::ZeroVector;
	};

	static FTally Check(
		const UFofuxoRetargetPose& Stored,
		const FIKRetargetPose& Current,
		const FReferenceSkeleton& Skeleton)
	{
		FTally Tally;
		Tally.Pelvis = Stored.PelvisOffset;

		const TArray<FTransform>& Local = Skeleton.GetRefBonePose();

		TSet<FName> Incoming;

		for (const TTuple<FName, FQuat>& Pair : Stored.LocalRotations)
		{
			const int32 Index = Skeleton.FindBoneIndex(Pair.Key);
			if (Index == INDEX_NONE)
			{
				Tally.NoSuchBone.Add(Pair.Key);
				continue;
			}

			// From "where the bone has to end up" to "how far it moves from the ref
			// pose here". If the two skeletons are the same, this gives back exactly
			// the delta that was over there.
			const FQuat Delta =
				(Local[Index].GetRotation().Inverse() * Pair.Value).GetNormalized();

			if (Delta.Equals(FQuat::Identity, Slack))
			{
				++Tally.AlreadyThere;
				continue;
			}

			Tally.Matching.Emplace(Pair.Key, Delta);
			Incoming.Add(Pair.Key);
		}

		for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
		{
			if (!Stored.LocalRotations.Contains(Skeleton.GetBoneName(Index)))
			{
				++Tally.Unmentioned;
			}
		}

		for (const TTuple<FName, FQuat>& Pair : Current.GetAllDeltaRotations())
		{
			if (!Pair.Value.Equals(FQuat::Identity, Slack) && !Incoming.Contains(Pair.Key))
			{
				++Tally.Cleared;
			}
		}

		Tally.Matching.Sort([](const TTuple<FName, FQuat>& A, const TTuple<FName, FQuat>& B)
		{
			return A.Key.LexicalLess(B.Key);
		});

		Tally.NoSuchBone.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });

		return Tally;
	}

	static FText BuildQuestion(
		const FTally& Tally,
		const FSide& Side,
		const UFofuxoRetargetPose& Stored)
	{
		FText Text = FText::Format(
			LOCTEXT("DiskQuestion",
				"Apply {0}'s pose to the pose \"{1}\" of the {2} side of {3}?\n\n"
				"{4} bones move away from {5}'s ref pose to land where the stored pose puts them."),
			FText::FromString(Stored.GetName()),
			FText::FromName(Side.Pose),
			NameOfSide(Side.Which),
			FText::FromString(Side.Asset->GetName()),
			FText::AsNumber(Tally.Matching.Num()),
			FText::FromString(Side.Mesh->GetName()));

		if (Tally.AlreadyThere > 0)
		{
			Text = FText::Format(
				LOCTEXT("DiskAlreadyThere", "{0}\n{1} are already in place and need nothing."),
				Text,
				FText::AsNumber(Tally.AlreadyThere));
		}

		if (Tally.NoSuchBone.Num() > 0)
		{
			Text = FText::Format(
				LOCTEXT("DiskNoSuchBone",
					"{0}\n{1} bones from the file do not exist in {2} and are left out (the names go to "
					"the Output Log)."),
				Text,
				FText::AsNumber(Tally.NoSuchBone.Num()),
				FText::FromString(Side.Mesh->GetName()));
		}

		if (Tally.Unmentioned > 0)
		{
			Text = FText::Format(
				LOCTEXT("DiskUnmentioned",
					"{0}\n{1} bones from here don't appear in the file and stay at this skeleton's ref pose."),
				Text,
				FText::AsNumber(Tally.Unmentioned));
		}

		if (Tally.Cleared > 0)
		{
			Text = FText::Format(
				LOCTEXT("DiskCleared",
					"{0}\n{1} bones you posed here go back to the ref pose -- applying is ending up alike, "
					"not adding up."),
				Text,
				FText::AsNumber(Tally.Cleared));
		}

		if (!Tally.Pelvis.IsNearlyZero())
		{
			Text = FText::Format(
				LOCTEXT("DiskPelvis",
					"{0}\n\nThe pelvis offset comes along ({1}). That is the only value in centimetres in "
					"the file: between characters of different sizes, check it afterwards."),
				Text,
				FText::FromString(Tally.Pelvis.ToCompactString()));
		}

		if (!Stored.Skeleton.IsEmpty())
		{
			Text = FText::Format(
				LOCTEXT("DiskWhereItCameFrom",
					"{0}\n\nThe file was saved from {1} ({2}).\nBones are matched by name: two skeletons "
					"following Unreal's convention match, one using another axis convention does not, and "
					"there is no conversion that fixes that."),
				Text,
				FText::FromString(Stored.Mesh),
				FText::FromString(Stored.Skeleton));
		}

		return FText::Format(LOCTEXT("DiskClosing", "{0}\n\nOne Ctrl+Z undoes all of it at once."), Text);
	}
}

FIKRetargetEditor* FFofuxoPoseOnDisk::EditorOfContext(const FToolMenuContext& Context)
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

void FFofuxoPoseOnDisk::BuildSection(UToolMenu* Menu)
{
	FToolMenuSection& Section = Menu->FindOrAddSection(
		"FofuxoPoseOnDisk", LOCTEXT("DiskSection", "From disk -- crosses projects"));

	Section.AddMenuEntry(
		"FofuxoSavePose",
		LOCTEXT("DiskSave", "Save this pose into an asset..."),
		LOCTEXT("DiskSaveTip",
			"Writes the pose of the side you are editing into a pose asset, which is a file like any "
			"other -- you can copy it into another project through the file explorer, or with Migrate.\n\n"
			"What gets written is each bone's final rotation, and not the delta. That is why the Manny's "
			"pose from here works on a MetaHuman: the delta is the correction measured from the ref pose "
			"of whoever made it, and the final rotation is the pose."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save"),
		FToolUIActionChoice(FToolUIAction(
			FToolMenuExecuteAction::CreateStatic(&FFofuxoPoseOnDisk::Save))));

	Section.AddMenuEntry(
		"FofuxoApplyPose",
		LOCTEXT("DiskApply", "Apply an asset's pose..."),
		LOCTEXT("DiskApplyTip",
			"Replaces the pose of the side you are editing with a pose asset's, matching bones by name.\n\n"
			"The question before writing says how many bones move, how many are already in place, how "
			"many are left out and how many go back to the ref pose."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.OpenFile"),
		FToolUIActionChoice(FToolUIAction(
			FToolMenuExecuteAction::CreateStatic(&FFofuxoPoseOnDisk::Apply))));
}

void FFofuxoPoseOnDisk::Save(const FToolMenuContext& Context)
{
	FIKRetargetEditor* Editor = EditorOfContext(Context);
	if (Editor == nullptr)
	{
		return;
	}

	const FofuxoDisk::FSide Side = FofuxoDisk::SideOfEditor(*Editor);
	if (!Side.Serves())
	{
		return;
	}

	const FIKRetargetPose& Pose = Side.AssetController->GetCurrentRetargetPose(Side.Which);
	const TMap<FName, FQuat>& Deltas = Pose.GetAllDeltaRotations();

	FString SuggestedName = FString::Printf(TEXT("FPOSE_%s_%s"),
		*Side.Mesh->GetName(), *Side.Pose.ToString());
	SuggestedName.RemoveSpacesInline();

	FSaveAssetDialogConfig Config;
	Config.DefaultPath = FPaths::GetPath(Side.Asset->GetPackage()->GetPathName());
	Config.DefaultAssetName = SuggestedName;
	Config.AssetClassNames.Add(UFofuxoRetargetPose::StaticClass()->GetClassPathName());
	Config.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
	Config.DialogTitleOverride = LOCTEXT("DiskSaveTitle", "Save the retarget pose");

	const FContentBrowserModule& Browser =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	const FString Chosen = Browser.Get().CreateModalSaveAssetDialog(Config);
	if (Chosen.IsEmpty())
	{
		return;
	}

	const FString PackagePath = FPackageName::ObjectPathToPackageName(Chosen);
	const FString AssetName = FPaths::GetBaseFilename(PackagePath, /*bRemovePath*/ true);

	UPackage* Package = CreatePackage(*PackagePath);
	if (Package == nullptr)
	{
		return;
	}

	UFofuxoRetargetPose* Stored = NewObject<UFofuxoRetargetPose>(
		Package, UFofuxoRetargetPose::StaticClass(), FName(*AssetName), RF_Public | RF_Standalone);

	const FReferenceSkeleton& Skeleton = Side.Mesh->GetRefSkeleton();
	const TArray<FTransform>& Local = Skeleton.GetRefBonePose();

	int32 Posed = 0;

	for (int32 Index = 0; Index < Skeleton.GetNum(); ++Index)
	{
		const FName Bone = Skeleton.GetBoneName(Index);

		const FQuat* Found = Deltas.Find(Bone);
		const FQuat Delta = Found != nullptr ? *Found : FQuat::Identity;

		if (!Delta.Equals(FQuat::Identity, FofuxoDisk::Slack))
		{
			++Posed;
		}

		Stored->LocalRotations.Add(
			Bone, (Local[Index].GetRotation() * Delta).GetNormalized());
	}

	Stored->PelvisOffset = Pose.GetRootTranslationDelta();
	Stored->PosedBones = Posed;
	Stored->Mesh = Side.Mesh->GetName();
	Stored->Skeleton = Side.Mesh->GetSkeleton() != nullptr ? Side.Mesh->GetSkeleton()->GetName() : FString();
	Stored->Retargeter = Side.Asset->GetName();
	Stored->Side = FofuxoDisk::NameOfSide(Side.Which).ToString();
	Stored->PoseName = Side.Pose.ToString();
	Stored->When = FDateTime::Now().ToString(TEXT("%d/%m/%Y"));

	FAssetRegistryModule::AssetCreated(Stored);
	Package->MarkPackageDirty();

	UE_LOG(LogFofuxoPoseOnDisk, Display,
		TEXT("Saved the pose \"%s\" of %s into %s: %d bones, %d posed."),
		*Side.Pose.ToString(), *Side.Mesh->GetName(), *PackagePath,
		Stored->LocalRotations.Num(), Posed);

	FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
		LOCTEXT("DiskSaved",
			"Saved {0} with {1}'s whole pose: {2} bones, of which {3} are posed.\n\n"
			"The asset is not on disk yet -- it is born dirty, like any new asset. Save the project for "
			"the file to exist."),
		FText::FromString(AssetName),
		FText::FromString(Side.Mesh->GetName()),
		FText::AsNumber(Stored->LocalRotations.Num()),
		FText::AsNumber(Posed)));
}

void FFofuxoPoseOnDisk::Apply(const FToolMenuContext& Context)
{
	FIKRetargetEditor* Editor = EditorOfContext(Context);
	if (Editor == nullptr)
	{
		return;
	}

	const FofuxoDisk::FSide Side = FofuxoDisk::SideOfEditor(*Editor);
	if (!Side.Serves())
	{
		return;
	}

	FOpenAssetDialogConfig Config;
	Config.AssetClassNames.Add(UFofuxoRetargetPose::StaticClass()->GetClassPathName());
	Config.bAllowMultipleSelection = false;
	Config.DialogTitleOverride = LOCTEXT("DiskApplyTitle", "Apply a retarget pose");

	const FContentBrowserModule& Browser =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	const TArray<FAssetData> Chosen = Browser.Get().CreateModalOpenAssetDialog(Config);
	if (Chosen.IsEmpty())
	{
		return;
	}

	UFofuxoRetargetPose* Stored = Cast<UFofuxoRetargetPose>(Chosen[0].GetAsset());
	if (Stored == nullptr)
	{
		return;
	}

	const FIKRetargetPose& Current = Side.AssetController->GetCurrentRetargetPose(Side.Which);

	const FofuxoDisk::FTally Tally =
		FofuxoDisk::Check(*Stored, Current, Side.Mesh->GetRefSkeleton());

	if (Tally.Matching.IsEmpty() && Tally.Cleared == 0 && Tally.Pelvis.IsNearlyZero())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("DiskNothing",
				"{0} changes nothing in {1}: the bones that match are already where the stored pose puts "
				"them, and the pose here is already cleared."),
			FText::FromString(Stored->GetName()),
			FText::FromString(Side.Mesh->GetName())));

		return;
	}

	for (const FName& Bone : Tally.NoSuchBone)
	{
		UE_LOG(LogFofuxoPoseOnDisk, Display,
			TEXT("\"%s\" is in %s but does not exist in %s -- out of the application."),
			*Bone.ToString(), *Stored->GetName(), *Side.Mesh->GetName());
	}

	const EAppReturnType::Type Answer = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FofuxoDisk::BuildQuestion(Tally, Side, *Stored),
		LOCTEXT("DiskTitle", "Apply retarget pose"));

	if (Answer != EAppReturnType::Yes)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("DiskTransaction", "Apply retarget pose"));

	Side.Asset->Modify();

	// A single reinitialization, at the end of the scope -- and not one per bone.
	const FScopedReinitializeIKRetargeter Reinitialize(Side.AssetController);

	// Clear before writing: whatever was left here and didn't come from the file
	// would make the pose the sum of the two, and not a copy of one.
	Side.AssetController->ResetRetargetPose(Side.Pose, TArray<FName>(), Side.Which);

	for (const TTuple<FName, FQuat>& Pair : Tally.Matching)
	{
		Side.AssetController->SetRotationOffsetForRetargetPoseBone(Pair.Key, Pair.Value, Side.Which);
	}

	if (!Tally.Pelvis.IsNearlyZero())
	{
		// The Reset has just left the offset at zero, so adding is the same as
		// assigning -- and adding is the only verb the controller offers.
		Side.AssetController->SetRootOffsetInRetargetPose(Tally.Pelvis, Side.Which);
	}

	UE_LOG(LogFofuxoPoseOnDisk, Display,
		TEXT("Applied %s to \"%s\" of the %s side of %s: %d bones written, %d already in place, %d left out."),
		*Stored->GetName(),
		*Side.Pose.ToString(),
		*FofuxoDisk::NameOfSide(Side.Which).ToString(),
		*Side.Asset->GetName(),
		Tally.Matching.Num(),
		Tally.AlreadyThere,
		Tally.NoSuchBone.Num());
}

#undef LOCTEXT_NAMESPACE
