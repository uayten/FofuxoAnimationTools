// Fofuxo -- Alt+R on the selected bones

#pragma once

#include "CoreMinimal.h"

class FIKRetargetEditor;
class FIKRetargetEditorController;

/**
 * Gives the selected bones' rotation in the retarget pose back to the ref pose.
 *
 * It is the same effect as the toolbar's "Reset Selected Bones", with two
 * differences that are the reason it exists:
 *
 * - **It works with the animation running.** The engine's command can only be
 *   executed in Editing Retarget Pose, and Live Retarget exists precisely to
 *   adjust outside it. Getting a finger wrong and having to leave the mode to
 *   undo it would mean losing the frame you were on.
 * - **It has a key.** Alt+R, as in Blender.
 *
 * It doesn't touch the attachment op's Offset. They are things of different
 * natures -- one is a rotation stored in the pose, the other a translation added
 * during the retarget -- and a key that erased both would have no way of asking
 * which of the two you wanted back.
 */
class FFofuxoResetRotation
{
public:

	/** Registers and releases the command set. The module is the one that calls. */
	static void Register();
	static void Forget();

	/**
	 * Puts the shortcut on this editor's command list, once only.
	 *
	 * It goes on the toolkit's list, and not the viewport's: that way the key works
	 * with the focus on any of the editor's panels -- the viewport, the hierarchy,
	 * the op stack -- which is where the bone selection may be.
	 */
	static void EnsureShortcut(FIKRetargetEditor& Editor);

	static bool Can(TWeakPtr<FIKRetargetEditorController> Weak);
	static void Reset(TWeakPtr<FIKRetargetEditorController> Weak);
};
