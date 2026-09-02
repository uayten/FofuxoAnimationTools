// Fofuxo -- straightening bones in the retarget pose

#include "FofuxoAlignBones.h"

#include "Engine/SkeletalMesh.h"
#include "Misc/ConfigCacheIni.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoAlign, Log, All);

namespace FofuxoAlign
{
	static const TCHAR* IniSection = TEXT("FofuxoRetargetProps");
	static const TCHAR* IniKey = TEXT("AlignMode");
	static const TCHAR* AxisKey = TEXT("WorldAxis");

	static bool bReadFromIni = false;
	static EFofuxoAlignMode ArmedMode = EFofuxoAlignMode::Selected;

	static bool bAxisReadFromIni = false;
	static EFofuxoWorldAxis ArmedAxis = EFofuxoWorldAxis::PlusX;

	/** All four, in the order they show up in the menu. */
	static const EFofuxoAlignMode Modes[] =
	{
		EFofuxoAlignMode::Selected,
		EFofuxoAlignMode::WithChildren,
		EFofuxoAlignMode::ToTheLast,
		EFofuxoAlignMode::ToWorld,
	};

	/** All six, in the order they show up in the menu. */
	static const EFofuxoWorldAxis Axes[] =
	{
		EFofuxoWorldAxis::PlusX,
		EFofuxoWorldAxis::MinusX,
		EFofuxoWorldAxis::PlusY,
		EFofuxoWorldAxis::MinusY,
		EFofuxoWorldAxis::PlusZ,
		EFofuxoWorldAxis::MinusZ,
	};

	/**
	 * The orientation that puts local X -- the bone's tip, in Unreal's convention
	 * -- on top of the chosen axis.
	 *
	 * These are not just any rotations taking one axis to another: each is a turn
	 * of a multiple of 90 degrees about a world axis, so the bone's other two axes
	 * land on world axes as well. An FRotationMatrix::MakeFromX would settle the
	 * tip and leave the spin around it up to the library -- and the spin around
	 * the weapon itself is exactly what cannot be left loose.
	 */
	static FQuat OrientationOfAxis(const EFofuxoWorldAxis Which)
	{
		// FRotator is (Pitch, Yaw, Roll): pitch turns about the world's Y, yaw
		// about its Z.
		switch (Which)
		{
		case EFofuxoWorldAxis::MinusX: return FRotator(0.0, 180.0, 0.0).Quaternion();
		case EFofuxoWorldAxis::PlusY:  return FRotator(0.0, 90.0, 0.0).Quaternion();
		case EFofuxoWorldAxis::MinusY: return FRotator(0.0, -90.0, 0.0).Quaternion();
		case EFofuxoWorldAxis::PlusZ:  return FRotator(90.0, 0.0, 0.0).Quaternion();
		case EFofuxoWorldAxis::MinusZ: return FRotator(-90.0, 0.0, 0.0).Quaternion();
		default:                       return FQuat::Identity;
		}
	}

	/** How many bones the mode needs selected to do anything. */
	static int32 MinimumBones(const EFofuxoAlignMode Which)
	{
		// Aligning a lone bone to its own orientation changes nothing, and a
		// button that accepts the click and does nothing is worse than a greyed
		// one.
		return Which == EFofuxoAlignMode::ToTheLast ? 2 : 1;
	}

	/**
	 * Does the mode throw the whole selection into a single orientation, in
	 * component space?
	 *
	 * The two that do -- ToTheLast and ToWorld -- are the same sum; only where the
	 * target orientation comes from differs.
	 */
	static bool HasSingleOrientation(const EFofuxoAlignMode Which)
	{
		return Which == EFofuxoAlignMode::ToTheLast || Which == EFofuxoAlignMode::ToWorld;
	}

	/** The pieces of the editor every operation here needs. */
	struct FTarget
	{
		UIKRetargeterController* AssetController = nullptr;
		USkeletalMesh* Mesh = nullptr;
		ERetargetSourceOrTarget Side = ERetargetSourceOrTarget::Source;

		bool Serve() const { return AssetController != nullptr && Mesh != nullptr; }
	};

	static FTarget TargetOfEditor(FIKRetargetEditor& Editor)
	{
		const TSharedRef<FIKRetargetEditorController> Controller = Editor.GetController();

		FTarget Target;
		Target.AssetController = Controller->AssetController;
		Target.Side = Controller->GetSourceOrTarget();

		if (Target.AssetController != nullptr)
		{
			Target.Mesh = Target.AssetController->GetPreviewMesh(Target.Side);
		}

		return Target;
	}

	/** The selected bones that exist in this skeleton, in the order they were clicked. */
	static void Selected(
		const FIKRetargetEditorController& Controller,
		const FReferenceSkeleton& Skeleton,
		TArray<int32>& OutIndices)
	{
		for (const FName& Bone : Controller.GetSelectedBones())
		{
			const int32 Index = Skeleton.FindBoneIndex(Bone);
			if (Index != INDEX_NONE)
			{
				OutIndices.AddUnique(Index);
			}
		}
	}

	/** Adds to the indices everything that descends from them. */
	static void WithTheChildren(const FReferenceSkeleton& Skeleton, TArray<int32>& Indices)
	{
		// A single forward pass: in the skeleton's list the parent always comes
		// before the child, so by the time the grandchild's turn arrives the child
		// is already in.
		const int32 HowMany = Skeleton.GetNum();

		for (int32 Index = 0; Index < HowMany; ++Index)
		{
			const int32 Parent = Skeleton.GetParentIndex(Index);
			if (Parent != INDEX_NONE && Indices.Contains(Parent))
			{
				Indices.AddUnique(Index);
			}
		}
	}

	/** The delta this bone has today in the current pose, or identity. */
	static FQuat DeltaOf(const TMap<FName, FQuat>& Deltas, const FName Bone)
	{
		const FQuat* Found = Deltas.Find(Bone);
		return Found != nullptr ? *Found : FQuat::Identity;
	}
}

FIKRetargetEditor* FFofuxoAlignBones::EditorOfContext(const FToolMenuContext& Context)
{
	const UAssetEditorToolkitMenuContext* FromEditor = Context.FindContext<UAssetEditorToolkitMenuContext>();
	if (FromEditor == nullptr)
	{
		return nullptr;
	}

	// The retarget editor's toolbar is the only place this entry was put on, but
	// the context is generic and nothing stops another toolbar inheriting from it.
	const TSharedPtr<FAssetEditorToolkit> Toolkit = FromEditor->Toolkit.Pin();
	if (!Toolkit.IsValid() || Toolkit->GetEditorName() != FName("IKRetargetEditor"))
	{
		return nullptr;
	}

	return static_cast<FIKRetargetEditor*>(Toolkit.Get());
}

EFofuxoAlignMode FFofuxoAlignBones::Mode()
{
	if (!FofuxoAlign::bReadFromIni)
	{
		FofuxoAlign::bReadFromIni = true;

		int32 Stored = static_cast<int32>(FofuxoAlign::ArmedMode);
		GConfig->GetInt(FofuxoAlign::IniSection, FofuxoAlign::IniKey, Stored, GEditorPerProjectIni);

		// An ini written by a version with more modes than this one must not arm a
		// mode that doesn't exist -- it would fall into the switch's default: and
		// the button would do nothing.
		if (Stored >= 0 && Stored < static_cast<int32>(UE_ARRAY_COUNT(FofuxoAlign::Modes)))
		{
			FofuxoAlign::ArmedMode = static_cast<EFofuxoAlignMode>(Stored);
		}
	}

	return FofuxoAlign::ArmedMode;
}

void FFofuxoAlignBones::ChooseMode(const EFofuxoAlignMode New)
{
	// The lazy read has to happen first: without this, the first call to Mode()
	// after this one would go to the ini and undo the choice.
	Mode();

	FofuxoAlign::ArmedMode = New;

	GConfig->SetInt(FofuxoAlign::IniSection, FofuxoAlign::IniKey,
		static_cast<int32>(New), GEditorPerProjectIni);
}

EFofuxoWorldAxis FFofuxoAlignBones::Axis()
{
	if (!FofuxoAlign::bAxisReadFromIni)
	{
		FofuxoAlign::bAxisReadFromIni = true;

		int32 Stored = static_cast<int32>(FofuxoAlign::ArmedAxis);
		GConfig->GetInt(FofuxoAlign::IniSection, FofuxoAlign::AxisKey, Stored, GEditorPerProjectIni);

		if (Stored >= 0 && Stored < static_cast<int32>(UE_ARRAY_COUNT(FofuxoAlign::Axes)))
		{
			FofuxoAlign::ArmedAxis = static_cast<EFofuxoWorldAxis>(Stored);
		}
	}

	return FofuxoAlign::ArmedAxis;
}

void FFofuxoAlignBones::ChooseAxis(const EFofuxoWorldAxis New)
{
	// The same trap as the mode: without forcing the read first, the next call to
	// Axis() would go to the ini and undo the choice.
	Axis();

	FofuxoAlign::ArmedAxis = New;

	GConfig->SetInt(FofuxoAlign::IniSection, FofuxoAlign::AxisKey,
		static_cast<int32>(New), GEditorPerProjectIni);

	// Picking an axis is saying you want to align to the world. Forcing two
	// clicks -- the mode and then the axis -- would only make the click on the
	// axis do nothing visible.
	ChooseMode(EFofuxoAlignMode::ToWorld);
}

FText FFofuxoAlignBones::AxisName(const EFofuxoWorldAxis Which)
{
	switch (Which)
	{
	case EFofuxoWorldAxis::MinusX: return LOCTEXT("AxisMinusX", "-X");
	case EFofuxoWorldAxis::PlusY:  return LOCTEXT("AxisPlusY", "+Y");
	case EFofuxoWorldAxis::MinusY: return LOCTEXT("AxisMinusY", "-Y");
	case EFofuxoWorldAxis::PlusZ:  return LOCTEXT("AxisPlusZ", "+Z");
	case EFofuxoWorldAxis::MinusZ: return LOCTEXT("AxisMinusZ", "-Z");
	default:                       return LOCTEXT("AxisPlusX", "+X");
	}
}

FText FFofuxoAlignBones::Label(const EFofuxoAlignMode Which)
{
	switch (Which)
	{
	case EFofuxoAlignMode::WithChildren:
		return LOCTEXT("AlignWithChildren", "Align with children");

	case EFofuxoAlignMode::ToTheLast:
		return LOCTEXT("AlignToTheLast", "Align to the last");

	case EFofuxoAlignMode::ToWorld:
		return FText::Format(LOCTEXT("AlignToWorld", "Align to {0}"), AxisName(Axis()));

	default:
		return LOCTEXT("Align", "Align");
	}
}

FText FFofuxoAlignBones::Tooltip(const EFofuxoAlignMode Which)
{
	switch (Which)
	{
	case EFofuxoAlignMode::WithChildren:
		return LOCTEXT("AlignWithChildrenTip",
			"The same aligning, taking along everything that descends from the selected bones: with "
			"the hand selected, the whole hand comes out open.");

	case EFofuxoAlignMode::ToTheLast:
		return LOCTEXT("AlignToTheLastTip",
			"Puts every selected bone pointing the same way as the last one you clicked. It is not "
			"each one's parent's axes, it is a single orientation -- that bone's.\n\n"
			"It is for the chain you have already fixed at the tip: you got the last phalanx right "
			"with the gizmo, select the others, click it last, and the three end up alike.\n\n"
			"The last is the last one Ctrl-clicked in the viewport, or the bottom one in the "
			"hierarchy list. It moves nobody's position.");

	case EFofuxoAlignMode::ToWorld:
		return FText::Format(LOCTEXT("AlignToWorldTip",
			"Puts the bone's axes on top of the world's axes, with its tip pointing at {0}. It looks "
			"neither at the parent nor at the rest of the pose.\n\n"
			"It is for weapons. A weapon bone is only any good if it is in the same orientation on "
			"the character and on the weapon, and \"same\" needs a reference that is neither of the "
			"two -- otherwise you end up adjusting one against the other. Align the hand's bone to "
			"the world here and the weapon's root bone in its own retargeter, and the two match "
			"without measuring anything, already in Running Retarget. The next weapon comes in "
			"aligned for free. Which axis you pick changes nothing, as long as it is the same on "
			"both assets.\n\n"
			"It is the bone lying down pointing at Blender's +Y: there the bone is drawn along its "
			"own Y, here Unreal's convention puts the length on X. If your skeleton uses another "
			"axis as its length, the axis name doesn't describe where the bone will point -- but the "
			"six choices are still six fixed orientations, and whichever looks right will serve.\n\n"
			"It doesn't touch the position."),
			AxisName(Axis()));

	default:
		return LOCTEXT("AlignTip",
			"Puts the selected bones with the same axes as each one's parent -- on a whole chain, "
			"that is the same as straightening it. Made for fingers: instead of getting each phalanx "
			"right with the gizmo, select the three and align.\n\n"
			"The same as Blender's Alt+R: it leaves the local rotation at zero, with the bone's axes "
			"landing on the parent's axes. It doesn't touch the position -- the bone still starts "
			"where it started.");
	}
}

FSlateIcon FFofuxoAlignBones::Icon(const EFofuxoAlignMode Which)
{
	// The straight-tangents icon serves both aligns to the parent; the two
	// world/last alignments are another operation and get their own, or only the
	// label would tell the modes apart in the toolbar.
	switch (Which)
	{
	case EFofuxoAlignMode::ToTheLast:
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Adjust");

	case EFofuxoAlignMode::ToWorld:
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.World");

	default:
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), "CurveEditor.StraightenTangents");
	}
}

bool FFofuxoAlignBones::Can(const FToolMenuContext& Context)
{
	FIKRetargetEditor* Editor = EditorOfContext(Context);
	if (Editor == nullptr)
	{
		return false;
	}

	const TSharedRef<FIKRetargetEditorController> Controller = Editor->GetController();

	return Controller->GetRetargeterMode() == ERetargeterOutputMode::EditRetargetPose
		&& Controller->GetSelectedBones().Num() >= FofuxoAlign::MinimumBones(Mode());
}

void FFofuxoAlignBones::Align(const FToolMenuContext& Context)
{
	FIKRetargetEditor* Editor = EditorOfContext(Context);
	if (Editor == nullptr)
	{
		return;
	}

	const TSharedRef<FIKRetargetEditorController> Controller = Editor->GetController();

	const FofuxoAlign::FTarget Target = FofuxoAlign::TargetOfEditor(*Editor);
	if (!Target.Serve())
	{
		return;
	}

	const FReferenceSkeleton& Skeleton = Target.Mesh->GetRefSkeleton();
	const TArray<FTransform>& Local = Skeleton.GetRefBonePose();

	TArray<int32> Targets;
	FofuxoAlign::Selected(*Controller, Skeleton, Targets);

	const EFofuxoAlignMode Armed = Mode();

	if (Armed == EFofuxoAlignMode::WithChildren)
	{
		FofuxoAlign::WithTheChildren(Skeleton, Targets);
	}

	if (Targets.Num() < FofuxoAlign::MinimumBones(Armed))
	{
		return;
	}

	// What is going to be written, decided in full before the first write goes
	// out: in the single-orientation modes one bone's sum uses its parent's
	// already-corrected pose, and mixing reads with writes in the pose's map would
	// read half of each.
	TArray<TTuple<FName, FQuat>> ToWrite;
	int32 Parentless = 0;

	if (FofuxoAlign::HasSingleOrientation(Armed))
	{
		const int32 HowMany = Skeleton.GetNum();

		const TMap<FName, FQuat>& Deltas =
			Target.AssetController->GetCurrentRetargetPose(Target.Side).GetAllDeltaRotations();

		TArray<FQuat> Component;
		Component.SetNum(HowMany);

		// In ToWorld the target is a constant, and there is nothing to measure. In
		// ToTheLast, the target is the orientation a bone *has today*, and then the
		// current pose has to be carried into component space before any write. The
		// parent comes before the child in the list, so one pass is enough.
		FQuat Orientation = FofuxoAlign::OrientationOfAxis(Axis());

		if (Armed == EFofuxoAlignMode::ToTheLast)
		{
			for (int32 Index = 0; Index < HowMany; ++Index)
			{
				const FQuat Rot = Local[Index].GetRotation()
					* FofuxoAlign::DeltaOf(Deltas, Skeleton.GetBoneName(Index));

				const int32 Parent = Skeleton.GetParentIndex(Index);
				Component[Index] = (Parent == INDEX_NONE ? Rot : Component[Parent] * Rot).GetNormalized();
			}

			// The order of Targets is the order the bones were clicked in:
			// EditBoneSelection only does AddUnique at the end of the list.
			Orientation = Component[Targets.Last()];
		}

		// Second pass, correcting now. A selected bone may be the child of another
		// selected bone, and then the parent has already turned by the time the
		// child's turn arrives -- that is why component space is rebuilt on the
		// way, and why the reference bone itself goes into the sum: if an ancestor
		// of it turned, it moved along and has to come back.
		const TSet<int32> Chosen(Targets);

		for (int32 Index = 0; Index < HowMany; ++Index)
		{
			const int32 Parent = Skeleton.GetParentIndex(Index);
			const FQuat FromParent = Parent == INDEX_NONE ? FQuat::Identity : Component[Parent];

			if (!Chosen.Contains(Index))
			{
				const FQuat Rot = Local[Index].GetRotation()
					* FofuxoAlign::DeltaOf(Deltas, Skeleton.GetBoneName(Index));

				Component[Index] = (FromParent * Rot).GetNormalized();
				continue;
			}

			// LocalRot = RefLocal.Rot * Delta, and what we want is
			// FromParent * LocalRot == Orientation.
			const FQuat New =
				(Local[Index].GetRotation().Inverse() * FromParent.Inverse() * Orientation).GetNormalized();

			ToWrite.Emplace(Skeleton.GetBoneName(Index), New);
			Component[Index] = Orientation;
		}
	}
	else
	{
		for (const int32 Index : Targets)
		{
			if (Skeleton.GetParentIndex(Index) == INDEX_NONE)
			{
				// A bone with no parent has nothing to align to: its "parent" is
				// the component itself, and zeroing there would lay the whole
				// character down.
				++Parentless;
				continue;
			}

			ToWrite.Emplace(
				Skeleton.GetBoneName(Index),
				Local[Index].GetRotation().Inverse().GetNormalized());
		}
	}

	if (ToWrite.IsEmpty())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AlignTransaction", "Align bones"));

	if (UIKRetargeter* Asset = Target.AssetController->GetAsset())
	{
		Asset->Modify();
	}

	// The retargeter reinitializes once, at the end of the scope -- and not once
	// per bone, which on a whole hand would be fifteen.
	const FScopedReinitializeIKRetargeter Reinitialize(Target.AssetController);

	for (const TTuple<FName, FQuat>& Pair : ToWrite)
	{
		Target.AssetController->SetRotationOffsetForRetargetPoseBone(Pair.Key, Pair.Value, Target.Side);
	}

	UE_LOG(LogFofuxoAlign, Display,
		TEXT("%s: %d bones of %s.%s"),
		*Label(Armed).ToString(),
		ToWrite.Num(),
		*Target.Mesh->GetName(),
		Parentless > 0 ? TEXT(" The root was left out, it has no parent to align to.") : TEXT(""));
}

bool FFofuxoAlignBones::DeltaToWorld(
	UIKRetargeterController& Controller,
	const ERetargetSourceOrTarget Side,
	const FName Bone,
	const EFofuxoWorldAxis Axis,
	FQuat& OutDelta)
{
	USkeletalMesh* Mesh = Controller.GetPreviewMesh(Side);
	if (Mesh == nullptr)
	{
		return false;
	}

	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();

	const int32 Index = Skeleton.FindBoneIndex(Bone);
	if (Index == INDEX_NONE)
	{
		return false;
	}

	const TArray<FTransform>& Local = Skeleton.GetRefBonePose();
	const TMap<FName, FQuat>& Deltas =
		Controller.GetCurrentRetargetPose(Side).GetAllDeltaRotations();

	// The parent's orientation in the current pose. Climbing up to the root and
	// back down, because what matters is this bone's chain -- the rest of the
	// skeleton doesn't enter the sum.
	TArray<int32> Chain;
	for (int32 Climbing = Skeleton.GetParentIndex(Index); Climbing != INDEX_NONE;
		Climbing = Skeleton.GetParentIndex(Climbing))
	{
		Chain.Add(Climbing);
	}

	FQuat FromParent = FQuat::Identity;
	for (int32 Step = Chain.Num() - 1; Step >= 0; --Step)
	{
		const int32 Who = Chain[Step];

		const FQuat Rot = Local[Who].GetRotation()
			* FofuxoAlign::DeltaOf(Deltas, Skeleton.GetBoneName(Who));

		FromParent = (FromParent * Rot).GetNormalized();
	}

	// LocalRot = RefLocal.Rot * Delta, and what we want is
	// FromParent * LocalRot == Orientation.
	OutDelta = (Local[Index].GetRotation().Inverse()
		* FromParent.Inverse()
		* FofuxoAlign::OrientationOfAxis(Axis)).GetNormalized();

	return true;
}

void FFofuxoAlignBones::BuildModeMenu(UToolMenu* Menu)
{
	FToolMenuSection& Section = Menu->FindOrAddSection(
		"FofuxoAlignModes", LOCTEXT("AlignModes", "What the button does"));

	for (const EFofuxoAlignMode Which : FofuxoAlign::Modes)
	{
		Section.AddMenuEntry(
			*FString::Printf(TEXT("FofuxoAlignMode%d"), static_cast<int32>(Which)),
			Label(Which),
			Tooltip(Which),
			Icon(Which),
			FToolUIActionChoice(FToolUIAction(
				FToolMenuExecuteAction::CreateLambda(
					[Which](const FToolMenuContext&) { FFofuxoAlignBones::ChooseMode(Which); }),
				FToolMenuCanExecuteAction(),
				FToolMenuGetActionCheckState::CreateLambda([Which](const FToolMenuContext&)
				{
					return FFofuxoAlignBones::Mode() == Which
						? ECheckBoxState::Checked
						: ECheckBoxState::Unchecked;
				}))),
			EUserInterfaceActionType::RadioButton);
	}

	// The axis in a section of its own, and not in a submenu hanging off the
	// mode's item: that way you can see which one is picked without hovering over
	// anything, and clicking one of them already arms ToWorld -- the only mode
	// that reads it.
	FToolMenuSection& OfAxes = Menu->FindOrAddSection(
		"FofuxoWorldAxes", LOCTEXT("AlignAxes", "Align to world: where the tip points"));

	for (const EFofuxoWorldAxis Which : FofuxoAlign::Axes)
	{
		OfAxes.AddMenuEntry(
			*FString::Printf(TEXT("FofuxoWorldAxis%d"), static_cast<int32>(Which)),
			AxisName(Which),
			FText::Format(
				LOCTEXT("AlignAxisTip",
					"The bone ends up with its axes on the world's axes and its tip at {0}. Picking "
					"this arms Align to world."),
				AxisName(Which)),
			FSlateIcon(),
			FToolUIActionChoice(FToolUIAction(
				FToolMenuExecuteAction::CreateLambda(
					[Which](const FToolMenuContext&) { FFofuxoAlignBones::ChooseAxis(Which); }),
				FToolMenuCanExecuteAction(),
				FToolMenuGetActionCheckState::CreateLambda([Which](const FToolMenuContext&)
				{
					// Ticked only when the mode is armed as well: with another mode
					// in the toolbar, a lit radio here would say the click is going
					// to align to the world, and it isn't.
					return FFofuxoAlignBones::Mode() == EFofuxoAlignMode::ToWorld
						&& FFofuxoAlignBones::Axis() == Which
						? ECheckBoxState::Checked
						: ECheckBoxState::Unchecked;
				}))),
			EUserInterfaceActionType::RadioButton);
	}
}

#undef LOCTEXT_NAMESPACE
