// Fofuxo -- the retarget pose as an asset

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "FofuxoRetargetPose.generated.h"

/**
 * A retarget pose stored outside the retargeter, so it can travel.
 *
 * "Copy pose" solves it within one project: it opens the other retargeters and
 * fishes the pose out of them. But the project next door is not in this one's
 * asset registry, and the Manny's pose -- the same one everywhere he is the
 * source -- stays trapped in the project where it was adjusted. This asset is a
 * file you copy.
 *
 * **What is stored is not the delta, it is each bone's final local rotation.**
 * The difference only shows up when the destination skeleton is another one,
 * which is exactly the MetaHuman case:
 *
 *     LocalRot(B) = RefLocal(B).Rot * Delta(B)
 *
 * The delta is the *correction* measured from the ref pose of whoever made it.
 * Carried to a skeleton whose ref pose is another, it reproduces the correction,
 * not the pose -- two similar but unequal A-poses land in two different places.
 * The local rotation reproduces the pose: the destination recomputes its own
 * delta with `Delta = RefLocal.Rot^-1 * Stored` and lands where the original
 * landed. When the two skeletons are the same, both sums give exactly the same
 * number -- so this is not an alternative mode, it is the general case.
 *
 * The pose is stored whole, bone by bone, and not only the ones that were posed.
 * An unposed bone doesn't mean "leave it as it is": on a skeleton with a
 * different ref pose, "as it is" is somewhere else. Storing the whole pose is
 * what makes the destination land on the same pose, and not on a mixture of the
 * two.
 *
 * What stays by bone name is the correspondence. Manny and MetaHuman match
 * because they follow the same naming and axis convention; a skeleton with
 * another bone-axis convention will not match, and no space conversion fixes
 * that.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Retarget Pose (Fofuxo)"))
class UFofuxoRetargetPose : public UObject
{
	GENERATED_BODY()

public:

	/** For you to write down what this file is. Nothing reads it. */
	UPROPERTY(EditAnywhere, Category = "Note", meta = (MultiLine = true))
	FString Note;

	/** Each bone's local rotation, in the parent's frame. The whole pose. */
	UPROPERTY(VisibleAnywhere, Category = "Pose")
	TMap<FName, FQuat> LocalRotations;

	/** The pelvis offset, in centimetres. The only size number here. */
	UPROPERTY(VisibleAnywhere, Category = "Pose")
	FVector PelvisOffset = FVector::ZeroVector;

	/** How many bones were actually posed when this was saved. */
	UPROPERTY(VisibleAnywhere, Category = "Where it came from")
	int32 PosedBones = 0;

	UPROPERTY(VisibleAnywhere, Category = "Where it came from")
	FString Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Where it came from")
	FString Skeleton;

	UPROPERTY(VisibleAnywhere, Category = "Where it came from")
	FString Retargeter;

	UPROPERTY(VisibleAnywhere, Category = "Where it came from")
	FString Side;

	UPROPERTY(VisibleAnywhere, Category = "Where it came from")
	FString PoseName;

	UPROPERTY(VisibleAnywhere, Category = "Where it came from")
	FString When;
};
