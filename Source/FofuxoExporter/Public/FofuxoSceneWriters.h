// Fofuxo

#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class USkeletalMesh;

/** A scene to write: one skeleton and several animations, in a single file. */
struct FFofuxoSceneRequest
{
	TArray<UAnimSequence*> Animations;

	/** Where the skeleton the animations move comes from. */
	USkeletalMesh* Mesh = nullptr;

	FString Path;

	/** How many metres one unit is worth. A centimetre is 0.01, Unreal's default. */
	double MetresPerUnit = 0.01;

	bool bYUp = false;

	/** Multiplies the size. 1 is the size as it stands in Unreal. */
	double Scale = 1.0;

	/**
	 * Whether the mesh goes along. Turned off, only the skeleton and the
	 * animations come out -- far smaller, and enough when the other side already
	 * has the good mesh. It also avoids the cost of converting heavy geometry.
	 */
	bool bWithMesh = true;

	/**
	 * glTF only: whether the rig comes out turned the same way as the FBX of the
	 * same target.
	 *
	 * glTF is always Y-up, but that alone doesn't settle the basis. The FBX, on
	 * its way to Unity, comes out at (-X, Z, -Y) of the Unreal position; the
	 * engine's glTF exporter comes out at (X, Z, Y). The two bases are the same
	 * one turned half a turn about the up axis, and since each exporter bakes its
	 * basis into every bone, a character from one format will not take an
	 * animation from the other.
	 *
	 * On, the glTF module turns the whole file into the FBX's basis. Off -- which
	 * is the Blender target -- the file comes out as the engine writes it, which
	 * is what Blender's importer expects.
	 */
	bool bLikeTheFbx = false;
};

DECLARE_DELEGATE_RetVal_TwoParams(bool, FFofuxoWriteScene, const FFofuxoSceneRequest&, FText&);

/**
 * Who writes a USD scene -- bound by the FofuxoUsdScene module, which only
 * exists where the engine's USD plugin is present.
 *
 * This detour exists so the exporter doesn't depend on USD at link time. Without
 * it, an FBX-only project would not load the plugin: the USD DLL would be
 * missing. Unbound, the delegate stays loose and the caller returns a message.
 */
FOFUXOEXPORTER_API FFofuxoWriteScene& FofuxoUsdSceneWriter();

/**
 * The same for glTF, bound by the FofuxoGltfScene module.
 *
 * Two delegates rather than one with a format parameter because the modules are
 * independent: whoever has USD may not have glTF, and the other way round.
 */
FOFUXOEXPORTER_API FFofuxoWriteScene& FofuxoGltfSceneWriter();
