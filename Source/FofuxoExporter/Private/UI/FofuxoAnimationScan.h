// Fofuxo -- finding a skeleton's animations

#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class USkeleton;

/**
 * Every Animation Sequence in the project that belongs to this skeleton.
 *
 * It filters by the asset registry's "Skeleton" tag, the same way USkeleton
 * does -- that way it only loads the ones that matter, and not the whole
 * project. Loading is the expensive half, so it shows a progress dialog past a
 * handful of assets: this runs before the window exists, and with four hundred
 * animations to load it is where the editor used to look dead before any click.
 */
TArray<UAnimSequence*> FofuxoAnimationsOfSkeleton(const USkeleton* Skeleton);
