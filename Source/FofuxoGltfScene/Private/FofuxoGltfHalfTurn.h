// Fofuxo -- the half turn that matches glTF to FBX
//
// The two exporters write the same scene in different bases.
//
// FBX leaves Unreal mirrored on Y (the engine's ConvertToFbxPos hands back
// (X, -Y, Z), which is the swap of left hand for right) and, on the Unity
// target, FofuxoFbxWriter also calls DeepConvertScene to take Z-up up to Y. The
// whole sum takes an Unreal point to (-X, Z, -Y).
//
// glTF goes out through the engine's FGLTFCoreUtilities, which swaps Y for Z
// directly: (X, Z, Y). There is no option -- the functions are static and the
// value is in the code.
//
// One basis is the other turned half a turn about the up axis: (-1, 1, -1). It
// is not a mirror (the determinant is +1), and it is not a rotation hung off the
// root: each exporter bakes its own basis into every bone, so the difference
// lives in all 167 local transforms and not in a single one. Turning the root
// node fixes the root and leaves the rest wrong -- which is why this pass
// touches the whole file.

#pragma once

#include "CoreMinimal.h"

#include "Builders/GLTFContainerBuilder.h"

/**
 * FGLTFContainerBuilder with two extra things.
 *
 * The first is reaching the binary buffer, which in the engine is protected: the
 * pass below has to rewrite the positions, the normals and the already-converted
 * curves. Inheriting is the clean route for that -- a protected member is
 * visible from the child.
 */
class FFofuxoGltfBuilder : public FGLTFContainerBuilder
{
public:

	using FGLTFContainerBuilder::FGLTFContainerBuilder;

	/**
	 * Turns the whole file half a turn about the up axis, leaving it in the same
	 * convention FBX writes for Unity.
	 *
	 * Call it after ProcessSlowTasks -- that is where the mesh and the
	 * animations become bytes -- and before WriteAllFiles.
	 *
	 * It returns false, having touched nothing halfway, when some piece of data
	 * that needed turning cannot be reached. Half a rotation would be worse than
	 * none: the file would come out with no error and with the body folded.
	 */
	bool ApplyHalfTurn(FText& OutError);
};
