// Fofuxo -- copying the retarget pose from another retargeter

#pragma once

#include "CoreMinimal.h"

class FIKRetargetEditor;
class UToolMenu;
enum class ERetargetSourceOrTarget : uint8;

struct FToolMenuContext;

/**
 * Brings another retargeter's retarget pose into this one, matching bones by
 * name.
 *
 * What this solves is a fix that doesn't travel. When every retarget in the
 * project starts from the same character -- the Manny -- the source side of all
 * of them has the same pose, and it was adjusted once, in one of them. In the
 * others it stays crooked, and there is nothing in the editor that carries the
 * adjustment from one asset to another: it is redoing it in the gizmo, the same
 * way, as many times as there are retargeters.
 *
 * On the target side the same holds whenever the characters share Unreal's
 * naming convention and are posed the same way -- which is the case of a cast
 * made by the same person.
 *
 * The pose is a map from FName to FQuat, and the copy is literal: the delta of
 * "hand_l" over there becomes the delta of "hand_l" here. There is no space
 * conversion and no guessing at correspondence -- a bone that doesn't exist on
 * both sides is left out, and the number of bones in each category shows up in
 * the question before anything is written.
 *
 * **The destination's pose is replaced, not merged.** Copying means ending up
 * alike: a bone you posed here that isn't posed over there goes back to the ref
 * pose. One Ctrl+Z undoes all of it at once.
 *
 * The pelvis offset comes along, and it is *not* by bone name: it is a vector in
 * centimetres. Between two retargets of the same Manny it is exactly what you
 * want; between characters of different sizes, it is the one number in this copy
 * that can arrive wrong.
 */
class FFofuxoCopyPose
{
public:

	/** Can it copy right now? It needs a mesh on the side being edited. */
	static bool Can(const FToolMenuContext& Context);

	/** The button's menu: one submenu per retargeter, one item per side and pose. */
	static void BuildMenu(UToolMenu* Menu);

private:

	/** The retarget editor that owns this toolbar, or nullptr if it isn't one. */
	static FIKRetargetEditor* EditorOfContext(const FToolMenuContext& Context);

	/**
	 * The sides and the poses of a single retargeter.
	 *
	 * Built on demand, when the mouse rests on the name: listing the poses forces
	 * the asset to load, and opening every retargeter in the project to draw a
	 * menu would be expensive for nothing.
	 */
	static void BuildSubmenuForOneRetargeter(UToolMenu* Menu, const FSoftObjectPath& Path);

	/** The click on an item: it checks, asks, and replaces the destination's pose. */
	static void Apply(
		const FToolMenuContext& Context,
		FSoftObjectPath Source,
		ERetargetSourceOrTarget SourceSide,
		FName SourcePose);
};
