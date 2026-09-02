// Fofuxo -- slicing an export into files

#pragma once

#include "CoreMinimal.h"

class UAnimSequence;

/** One file's worth of an export: what goes in it, and where it goes. */
struct FFofuxoBatch
{
	TArray<UAnimSequence*> Animations;

	FString Path;
};

/**
 * Slices the animations into files.
 *
 * A single file doesn't scale: the animations all stay alive in the same scene
 * until the end, and writing is one call that can be neither split nor
 * cancelled. Measured here in FBX with 477 takes: 8.1 GB peak and a 690 MB file.
 * In batches the peak falls in proportion to the batch.
 *
 * `TakesPerFile` of 0 means "all in the same one", which is how the plugin was
 * born and is still what Blender wants: one file, several named takes.
 *
 * An empty animation list is not an error: it is the "mesh only" request, and it
 * comes back as a single batch with an empty list and the base path untouched.
 *
 * The naming rule is in the paths this returns: a single file keeps the plain
 * name, several get `_01`, `_02` before the extension, and one animation per
 * file makes each file carry that animation's name -- `Thing_37` would say
 * nothing and the animation's name says everything.
 */
TArray<FFofuxoBatch> FofuxoSliceIntoBatches(
	const TArray<UAnimSequence*>& Animations,
	int32 TakesPerFile,
	const FString& BasePath);
