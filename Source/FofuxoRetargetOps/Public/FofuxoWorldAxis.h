// Fofuxo -- the world axis a bone is aligned to

#pragma once

#include "CoreMinimal.h"

#include "FofuxoWorldAxis.generated.h"

/**
 * Where the bone's tip points when it is aligned to the world.
 *
 * "The bone's tip" is local X, which is the convention of Unreal's skeleton. On
 * a skeleton using another axis as its length the choice still holds -- these
 * are six fixed, repeatable orientations, and the one that looks right is the
 * one that serves -- it is just that then the item's name doesn't describe where
 * the bone will point.
 *
 * It lives in the runtime module because two things read from here: the Align
 * button, which is editor-side, and the attachments op, which is saved inside
 * the retargeter.
 */
UENUM(BlueprintType)
enum class EFofuxoWorldAxis : uint8
{
	PlusX  UMETA(DisplayName = "+X"),
	MinusX UMETA(DisplayName = "-X"),
	PlusY  UMETA(DisplayName = "+Y"),
	MinusY UMETA(DisplayName = "-Y"),
	PlusZ  UMETA(DisplayName = "+Z"),
	MinusZ UMETA(DisplayName = "-Z"),
};
