// Fofuxo -- the section on the IK Retargeter's toolbar

#pragma once

#include "CoreMinimal.h"

class FFofuxoMirrorPose;
class FIKRetargetEditor;

struct FToolMenuContext;

/**
 * The Fofuxo section on the IK Retargeter's toolbar: every button of this plugin
 * that lives up there, in one place.
 *
 * It used to live inside the module, and there was no name to search for -- "the
 * toolbar" was a private method of a class called after the module. Every other
 * feature of this plugin is one FofuxoXxx.h/.cpp pair, and this one now is too.
 *
 * The attachments have no button here: what turns them on and off is the Enable
 * Op of "Preview Attachments (Fofuxo)", on the op stack, which is where the list
 * lives. Two switches for one light would be one too many.
 */
class FFofuxoToolbar
{
public:

	/**
	 * Extends the retarget editor's toolbar. `Owner` is the module, which is what
	 * UnregisterOwner takes back at shutdown.
	 *
	 * ExtendMenu works before the toolbar exists: the retarget editor only
	 * registers its own when the first asset opens.
	 */
	static void Register(void* Owner, FFofuxoMirrorPose* Mirror);

	/** The retarget editor that owns this toolbar, or nullptr if it isn't one. */
	static FIKRetargetEditor* EditorOfContext(const FToolMenuContext& Context);
};
