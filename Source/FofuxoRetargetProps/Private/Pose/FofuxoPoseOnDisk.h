// Fofuxo -- saving and applying the retarget pose as an asset

#pragma once

#include "CoreMinimal.h"

class FIKRetargetEditor;
class UToolMenu;

struct FToolMenuContext;

/**
 * The two ends of UFofuxoRetargetPose: writing the open pose into an asset, and
 * bringing an asset's pose back.
 *
 * They live in the same menu as "Copy pose", in a section of their own, because
 * they are the same question with a different reach: that one copies from
 * another retargeter in *this* project, this one copies from a file, which
 * crosses projects.
 */
class FFofuxoPoseOnDisk
{
public:

	/** Puts the "From disk" section into the Copy pose button's menu. */
	static void BuildSection(UToolMenu* Menu);

private:

	/** The retarget editor that owns this toolbar, or nullptr if it isn't one. */
	static FIKRetargetEditor* EditorOfContext(const FToolMenuContext& Context);

	/** Asks where to save and writes the pose of the side being edited. */
	static void Save(const FToolMenuContext& Context);

	/** Asks which asset and replaces the pose of the side being edited with it. */
	static void Apply(const FToolMenuContext& Context);
};
