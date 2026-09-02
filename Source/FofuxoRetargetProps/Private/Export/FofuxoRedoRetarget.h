// Fofuxo -- redoing the retarget of whatever was already exported

#pragma once

#include "CoreMinimal.h"

class FIKRetargetEditor;

/**
 * An "Export Selected Animations" that picks the animations by itself: the ones
 * this retarget has already produced once.
 *
 * The manual work it saves is the selecting. After fixing the retargeter,
 * everything that came out of it is wrong, and redoing it means finding, in the
 * source's list, exactly the same ones as before -- seventy-odd, in the
 * Lizardmen's case.
 *
 * The one that knows which they were is the project itself: the batch retarget
 * duplicates the animation under the same name, only on the target's skeleton.
 * So every AnimSequence of the target skeleton that has a namesake on the source
 * skeleton has already been through here.
 *
 * The button lives in the Asset Browser tab, right next to Export Selected
 * Animations, which is the button it imitates. That tab is a Slate widget built
 * by hand by IKRigEditor, with no extension point at all -- so the way in is to
 * push a slot into its column after it exists, and that is why this needs
 * somebody calling EnsureButton every so often.
 */
class FFofuxoRedoRetarget
{
public:

	/** Puts the button on this editor's Asset Browser tab, if it isn't there yet. */
	static void EnsureButton(FIKRetargetEditor& Editor);

	/** The click: it gathers what was already exported, asks, and redoes it. */
	static void OnClicked(FIKRetargetEditor& Editor);

	/**
	 * Takes back every button already placed.
	 *
	 * Mandatory at module shutdown: the widget and its lambdas live in this DLL,
	 * and a tab that kept it after the unload -- Live Coding, for instance --
	 * would call code that no longer exists.
	 */
	static void Forget();
};
