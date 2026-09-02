// Fofuxo -- drawing and hitting bones in the retarget viewport

#pragma once

#include "CoreMinimal.h"

class FIKRetargetEditor;
class FIKRetargetEditorController;
class FPrimitiveDrawInterface;
class FSceneView;
class FViewport;

class HHitProxy;

/**
 * Two things about a bone in the viewport: hitting it with the mouse, and seeing
 * it.
 *
 * **Hitting** is the real problem, and it has nothing to do with looks. Unreal
 * selects bones by hit proxy: everything drawn is stamped with the identity of
 * whoever drew it, and the click reads the pixel under the cursor. Since a bone
 * is drawn as a tiny sphere and a few thin cones, the clickable area is just as
 * thin -- in a hand, seen from a distance, it is aiming at one pixel.
 *
 * Blender asks for no aim: it picks the bone nearest the click. That is what
 * BoneNearCursor() does, and it does it with the same material Unreal already
 * works with: instead of reading *one* hit proxy pixel, it reads a box of twenty
 * or so pixels around the cursor and hands back the bone proxy nearest the
 * centre. Clicking near is now enough, and since what comes out of there is a
 * real proxy, the one that selects is still the engine's own mode -- details
 * panel, hierarchy and gizmo all update by themselves, with no parallel path.
 *
 * That applies always, in both modes, and has no toggle.
 *
 * **Seeing** is the toggle. Unreal's bone is drawn in world units: the same size
 * for the femur and the phalanx, which in the hand becomes a grey ball and, as
 * the camera pulls back, vanishes. Blender's in *stick* mode is a thin line with
 * a circle at the joint and -- this is what matters -- has a constant size *on
 * screen*: pulling the camera back neither fattens it nor loses it.
 *
 * So the sticks are two things at once:
 *
 * 1. The engine's drawing shrinks, through the retargeter's own `BoneDrawSize` --
 *    the same value as the slider under Character > Bones in the viewport.
 * 2. A line and a circle of constant on-screen size go on top.
 *
 * Shrinking rather than hiding is deliberate: **the bone's hit proxy lives in the
 * engine's drawing.** Hiding the drawing would delete the ability to click along
 * with it, and there is no hanging an IKRigEditor bone identity off a drawing of
 * ours -- its proxy type is not exported. Shrunk, it is still there, covered by
 * the stick, and the proximity search finds it.
 *
 * A consequence of `BoneDrawSize` living in the asset: **saving the RTG with the
 * sticks on stores the shrunken size.** Turning them off restores the value and
 * the next save fixes it; and if it does end up wrong, it is the same slider
 * under Character > Bones.
 */
class FFofuxoBonesOnScreen
{
public:

	/** The sticks' toggle, stored in the ini. */
	static bool IsOn();
	static void Toggle();

	/**
	 * Keeps the engine's drawing shrunk while the sticks are on, and gives the
	 * previous size back when they go. It comes from the half-second walk.
	 */
	static void Follow(FIKRetargetEditor& Editor);

	/** Gives the previous size back in every retargeter we touched. */
	static void Forget();

	/** The sticks on both characters. It does nothing with the toggle off. */
	static void Draw(
		const FIKRetargetEditorController& Who,
		const FSceneView* View,
		FPrimitiveDrawInterface* PDI);

	/**
	 * A retargeter bone proxy within the search radius, the one nearest the
	 * cursor. nullptr if there is none.
	 */
	static HHitProxy* BoneNearCursor(FViewport& Viewport, int32 X, int32 Y);

	/** Whether this proxy is a retargeter bone -- by the type's name, which is what we have. */
	static bool IsBoneProxy(HHitProxy* Proxy);
};
