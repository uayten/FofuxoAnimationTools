// Fofuxo -- a second viewport window, locked to the source bone

#pragma once

#include "CoreMinimal.h"

class FIKRetargetEditor;

/**
 * A second viewport tab in the retarget editor, looking at the source bone.
 *
 * The problem it solves is that it doesn't all fit in one camera. Adjusting a
 * target finger wants the camera right on the finger; checking the source
 * finger, which is the reference, wants the camera on the other character, on
 * the other side of the screen. With one viewport, every finger costs two camera
 * trips, and you never see both at once.
 *
 * The scene is the *same* -- both characters, the same animation, the same
 * frame. The only difference is the camera, and that is why the two viewports
 * never disagree.
 *
 * The new viewport has no gizmo: it gets an FEditorModeTools of its own, and the
 * retarget editor's modes (Live Retarget included) live in the toolkit's, which
 * is the main viewport's. That is deliberate -- two gizmos for the same bone
 * would be two answers to one drag -- and it is what makes this one purely for
 * looking and orbiting.
 *
 * **The camera follows the bone every frame.** It is not a one-off framing: if
 * the animation runs and the character walks, it is the ground that goes by, not
 * the hand that escapes. The distance and the orientation are the ones you left
 * -- the zoom and the orbit stay yours.
 *
 * The source bone comes out of the target's selection through the chain mapping:
 * find the target chain containing the clicked bone, and the counterpart is the
 * bone at the same proportional position in the mapped chain. With no chain that
 * serves, it tries the same name on the source; failing that, the pelvis.
 *
 * The tab doesn't come back on its own when the editor reopens: its registration
 * happens half a second after the editor opens, and the saved layout was already
 * restored before that. It is in Window, and on the "Source viewport" button of
 * the Fofuxo toolbar.
 */
class FFofuxoSourceViewport
{
public:

	/** This tab's FTabId. */
	static const FName TabId;

	/**
	 * Puts the tab's registration on this editor, once only.
	 *
	 * Called from the same half-second walk that looks after the attachments:
	 * there is no "a retarget editor opened" event to listen to from outside, and
	 * the toolkit's RegisterTabSpawners belongs to IKRigEditor.
	 */
	static void EnsureTab(FIKRetargetEditor& Editor);

	/** Opens (or brings forward) this editor's tab. */
	static void Open(FIKRetargetEditor& Editor);

	/**
	 * Closes the tabs and releases the registrations.
	 *
	 * Without this, a Live Coding pass would leave behind a tab whose widget lives
	 * in this DLL, and the first frame after the unload would call code that no
	 * longer exists.
	 */
	static void Forget();
};
