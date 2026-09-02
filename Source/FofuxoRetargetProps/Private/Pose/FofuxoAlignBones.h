// Fofuxo -- straightening bones in the retarget pose

#pragma once

#include "CoreMinimal.h"
#include "FofuxoWorldAxis.h"
#include "Textures/SlateIcon.h"

class FIKRetargetEditor;
class UIKRetargeterController;
class UToolMenu;

enum class ERetargetSourceOrTarget : uint8;

struct FToolMenuContext;

/** What the Align button does when clicked. It lives in the ini, between sessions. */
enum class EFofuxoAlignMode : uint8
{
	/** The selected bones only. */
	Selected,

	/** The selected ones and everything below them. */
	WithChildren,

	/** The whole selection takes the orientation the last clicked bone had. */
	ToTheLast,

	/** Each selected bone's axes land on top of the world's axes. */
	ToWorld,
};

// EFofuxoWorldAxis moved house: it is now a UENUM in the runtime module, in
// FofuxoWorldAxis.h, because the attachments op also picks an axis and needs to
// save that choice inside the retargeter. The values and the names are the same,
// so whatever was already in the ini still holds.

/**
 * Straightens the retarget pose in a way the gizmo cannot reach.
 *
 * Three different problems, and that is why the button has modes.
 *
 * The first one is the finger: aligning phalanx by phalanx by eye is precision
 * work that almost never comes out truly straight -- except that "straight" here
 * has an exact definition, which is the bone's local rotation being identity.
 * The retarget pose stores, per bone, a local delta post-multiplied into the ref
 * pose:
 *
 *     LocalRot(B) = RefLocal(B).Rot * Delta(B)
 *
 * So "the child with the parent's axes" is not a geometry sum, it is a one-line
 * equation: LocalRot has to come out as identity, hence Delta = RefLocal.Rot^-1.
 * It depends neither on the parent's pose nor on the rest of the chain, and that
 * is why the order the bones are straightened in doesn't matter. It is Blender's
 * Alt+R, and it is the Selected and WithChildren modes.
 *
 * What this does *not* do is straighten the position: the bone stays at the
 * offset the ref pose gave it. On a normal hand, where the phalanges all point
 * down the same local axis, equal orientation already gives a straight finger --
 * which is the use case. On a skeleton where the phalanges are born crooked
 * relative to each other, what is left is the twist that was in the mesh from
 * the start, and no rotation takes that away.
 *
 * The second problem is the chain that is already right on one bone and wrong on
 * the others: you got the last phalanx right with the gizmo and want the two
 * behind it to match. There the target is not each one's parent, it is a single
 * orientation, measured in component space. Since a corrected bone may be the
 * child of another corrected bone, this *does* depend on order, and the sum
 * comes out in one pass from root to leaves:
 *
 *     Delta(B) = RefLocal(B).Rot^-1 * CS(B's parent)^-1 * Target
 *
 * with CS(parent) already recomputed. That is the ToTheLast mode. The reference
 * bone itself goes into the sum: if an ancestor of it was turned, it moved along
 * and has to come back -- in the end, the whole selection points the same way.
 *
 * The third is the weapon. A weapon bone is only any good if it is in the *same*
 * orientation on the character and on the weapon, and "same" needs a reference
 * that is neither of the two, or each asset gets adjusted against the other and
 * nothing ever closes. That reference is the world: the ToWorld mode is ToTheLast
 * with the Target pinned to a constant, and then the bone ends up with its own
 * axes on top of the world's axes. It is the bone lying down pointing at
 * Blender's +Y -- there the bone is drawn along its own Y, here Unreal's
 * convention puts the length on X. The state is the same; only where each
 * program draws the bone differs.
 *
 * The constant isn't always identity because not everyone wants the weapon
 * pointing the same way: EFofuxoWorldAxis picks between six, and identity is
 * +X's. *Which* of them changes nothing, as long as it is the same on both
 * assets -- what matters is that it is constant, and not a measurement taken
 * from one of them.
 *
 * Aligned like that, the two match without anyone measuring anything, and they
 * match in Running Retarget, which is where it counts.
 */
class FFofuxoAlignBones
{
public:

	/** The armed mode. Read from the ini on the first call. */
	static EFofuxoAlignMode Mode();

	/** Arms another mode and writes it to the ini. */
	static void ChooseMode(EFofuxoAlignMode New);

	/** The axis chosen for the ToWorld mode. Read from the ini on the first call. */
	static EFofuxoWorldAxis Axis();

	/** Picks the axis, writes it to the ini, and arms ToWorld along with it -- the only mode that uses it. */
	static void ChooseAxis(EFofuxoWorldAxis New);

	/** "+X", "-Y"... */
	static FText AxisName(EFofuxoWorldAxis Which);

	/**
	 * The button's label -- it changes with the mode, which is how you see which
	 * one is armed.
	 *
	 * In ToWorld the axis goes into the label: without it the toolbar wouldn't
	 * say where the click is going to point the bone, and there would be six
	 * different things under one name.
	 */
	static FText Label(EFofuxoAlignMode Which);
	static FText Tooltip(EFofuxoAlignMode Which);
	static FSlateIcon Icon(EFofuxoAlignMode Which);

	/** Are we in Editing Retarget Pose, with enough bones selected? */
	static bool Can(const FToolMenuContext& Context);

	/** The button's click: it does whatever the armed mode says. */
	static void Align(const FToolMenuContext& Context);

	/** The little three-dot menu, which is where the mode is changed. */
	static void BuildModeMenu(UToolMenu* Menu);

	/**
	 * The delta that puts a bone's axes on the world's axes, in the current pose.
	 *
	 * The same sum as the ToWorld mode, for a single bone and without going
	 * through the viewport's selection -- it is what the attachments op's Align
	 * button uses, which knows the bone's name and doesn't need anyone to select
	 * it.
	 *
	 * It writes nothing: the caller decides the transaction, which is what allows
	 * aligning both characters in a single Ctrl+Z.
	 *
	 * @return false if the bone does not exist in this skeleton.
	 */
	static bool DeltaToWorld(
		UIKRetargeterController& Controller,
		ERetargetSourceOrTarget Side,
		FName Bone,
		EFofuxoWorldAxis Axis,
		FQuat& OutDelta);

private:

	/** The retarget editor that owns this toolbar, or nullptr if it isn't one. */
	static FIKRetargetEditor* EditorOfContext(const FToolMenuContext& Context);
};
