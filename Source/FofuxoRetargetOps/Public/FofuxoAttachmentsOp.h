// Fofuxo -- the preview attachments, stored in the retargeter

#pragma once

#include "Animation/BoneReference.h"
#include "FofuxoWorldAxis.h"
#include "Retargeter/IKRetargetOps.h"
#include "Retargeter/IKRetargetSettings.h"

#include "FofuxoAttachmentsOp.generated.h"

/**
 * Which of the viewport's two characters an attachment shows up on.
 *
 * The engine's ERetargetSourceOrTarget only has the two, and it was missing the
 * most common case of all: the same weapon on both sides, to compare how it sits
 * on the source and on the target. It used to be two identical rows, and now it
 * is one.
 */
UENUM(BlueprintType)
enum class EFofuxoCharacter : uint8
{
	Source,
	Target,
	Both,
};

/**
 * An asset hung off a bone, for the viewport only.
 *
 * It is not the same as the skeleton editor's Add Preview Asset: that one lives
 * on the USkeletalMesh and the USkeleton, and so it disappears when the rig is
 * reimported as a new asset. This one lives in the retargeter, which is the
 * asset that survives swapping both characters.
 */
USTRUCT(BlueprintType)
struct FFofuxoAttachment
{
	GENERATED_BODY()

	/**
	 * Unticked, this whole row stops acting and stays stored.
	 *
	 * It is for adjusting one at a time: with the weapon on the back and the one
	 * in the hand up at once, one hides the other exactly when you want to see
	 * the fit. The checkbox sits in the row's header, so it can be turned off and
	 * on without opening either.
	 *
	 * **It removes the attachment from the viewport and it also removes the
	 * Offset, which comes out in the animation.** It used to be viewport-only,
	 * and that was worse: two rows on the same bone add their offsets, so
	 * unticking one hid the mesh and left the bone pushed by a value that was no
	 * longer in sight. An unticked row does nothing -- and the price of that is
	 * that unticking here changes what the export produces.
	 */
	UPROPERTY(EditAnywhere, Category = "Attachment")
	bool bShow = true;

	/** Which viewport characters this attachment shows up on. */
	UPROPERTY(EditAnywhere, Category = "Attachment")
	EFofuxoCharacter Character = EFofuxoCharacter::Both;

	/**
	 * The source bone it hangs off.
	 *
	 * There are two fields and not one because the two skeletons almost never
	 * call the same bone by the same name -- if they did, there would be no
	 * retarget to do. What shows up here is the bone list of that side, and each
	 * field only appears when that side is in play.
	 */
	UPROPERTY(EditAnywhere, Category = "Attachment",
		meta = (EditCondition = "Character != EFofuxoCharacter::Target", EditConditionHides))
	FBoneReference SourceBone;

	/** The target bone it hangs off. */
	UPROPERTY(EditAnywhere, Category = "Attachment",
		meta = (EditCondition = "Character != EFofuxoCharacter::Source", EditConditionHides))
	FBoneReference TargetBone;

	/** What to hang. */
	UPROPERTY(EditAnywhere, Category = "Attachment",
		meta = (AllowedClasses = "/Script/Engine.StaticMesh,/Script/Engine.SkeletalMesh"))
	TSoftObjectPtr<UObject> Asset;


	/**
	 * Moves the target bone, and comes out in exported animations.
	 *
	 * This is of a different nature than Align to world, and the difference is
	 * worth knowing. Aligning writes into the *retarget pose*, which is where the
	 * definition of neutral lives -- and that is why its effect comes out on
	 * every frame for free. The retarget pose, though, stores a rotation per bone
	 * and a single root translation: translation per bone doesn't fit there. So
	 * this one is an offset added *during* the retarget, after the pose is
	 * computed, which is what the engine's Pin Bone LocalOffset does.
	 *
	 * Two consequences that are not bugs:
	 *
	 * - "Reset Selected Bones" clears the rotation and doesn't clear this, and
	 *   "Copy pose" carries the rotation into another RTG and doesn't carry this.
	 * - In Editing Retarget Pose this doesn't show up: the retarget isn't running
	 *   there, and this value only exists while it runs.
	 *
	 * The offset is stated **in the parent's frame**, so it follows the hand when
	 * the hand turns instead of sliding off it -- and it does not follow the
	 * bone's own rotation. That second half is the one that matters, and it cost
	 * a bug: stated in the bone's own frame, the offset turns along with the
	 * animation, and the bone starts describing an arc around where it was
	 * *before* the offset. In the viewport that looks like a weapon spinning with
	 * its pivot in the old place. In the parent's frame the value doesn't depend
	 * on the animated rotation: the bone sits still in the new place, and the
	 * animation turns from there -- which is what "I moved the bone" means.
	 *
	 * Two rows on the same bone add their offsets. There is no reason to have
	 * two, and the sum is the only result that doesn't invent a tie-break rule.
	 *
	 * Target only. The source is the retarget's input -- an op reads its pose and
	 * doesn't write it, and so there is no source offset that could apply.
	 */
	UPROPERTY(EditAnywhere, Category = "Attachment",
		meta = (DisplayName = "Offset the bone -- comes out in the animation"))
	FVector Offset = FVector::ZeroVector;

	/**
	 * Touches only the attached asset, and never the bone.
	 *
	 * The difference from Offset is what each of them fixes. Offset fixes *the
	 * bone*: it goes into the retarget and comes out in exported animations. This
	 * one fixes *the mesh*: it is the preview component's relative transform, it
	 * dies in the viewport and reaches no animation at all.
	 *
	 * It is for two things, and only those:
	 *
	 * 1. A crooked pivot. If the sword model's origin isn't at the grip, the one
	 *    that is wrong is the model -- and offsetting the bone to compensate would
	 *    bake that compensation into every exported animation, hiding the problem
	 *    instead of solving it.
	 * 2. The source side. An op reads the source's pose and writes the target's,
	 *    so Offset cannot reach the source. If the Manny's sword is in the wrong
	 *    place in the viewport, this is the only way.
	 *
	 * Outside those two cases, Offset is the right one.
	 */
	UPROPERTY(EditAnywhere, Category = "Attachment",
		meta = (DisplayName = "Preview fit -- does not come out in the animation"))
	FTransform PreviewFit = FTransform::Identity;

	/**
	 * Where the bone's tip will point when you click Align to world.
	 *
	 * *Which* axis changes nothing, as long as it is the same on the character
	 * and on the weapon -- what matters is that the reference is external to both
	 * and not measured in either. That is why the button aligns both sides at
	 * once when the attachment is on Both: it is the same constant on the two,
	 * and then they match without anyone measuring anything.
	 */
	UPROPERTY(EditAnywhere, Category = "Attachment")
	EFofuxoWorldAxis Axis = EFofuxoWorldAxis::PlusX;
};

/** The list, which is what shows up in the details panel when the op is selected. */
USTRUCT(BlueprintType, meta = (DisplayName = "Preview Attachments (Fofuxo)"))
struct FFofuxoAttachmentsOpSettings : public FIKRetargetOpSettingsBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Attachments")
	TArray<FFofuxoAttachment> Attachments;

	virtual void CopySettingsAtRuntime(const FIKRetargetOpSettingsBase* InSettingsToCopyFrom) override
	{
		Attachments = static_cast<const FFofuxoAttachmentsOpSettings*>(InSettingsToCopyFrom)->Attachments;
	}

#if WITH_EDITORONLY_DATA
	/**
	 * Which skeleton the bone picker takes its names from.
	 *
	 * The answer comes from the property's name alone, without looking at the
	 * row: splitting it into two fields took the doubt away with it. The source
	 * field lists the Manny's bones, the target field lists the character's, and
	 * neither depends on the attachment being on Source, Target or Both.
	 */
	virtual USkeleton* GetSkeleton(const FName InPropertyName) override
	{
		if (InPropertyName == GET_MEMBER_NAME_CHECKED(FFofuxoAttachment, SourceBone))
		{
			return const_cast<USkeleton*>(SourceSkeletonAsset);
		}

		return const_cast<USkeleton*>(TargetSkeletonAsset);
	}
#endif
};

/**
 * Stores the attachment list inside the retargeter, and applies each one's
 * offset to the target bone while the retarget runs.
 *
 * The reason it is an op, and not a separate asset, is that UIKRetargeter is the
 * engine's and takes no new properties. The op stack is the only place inside it
 * that accepts third-party data: an FInstancedStruct, saved in the asset, with a
 * details panel of its own and undo/redo for free.
 *
 * It was born as a data carrier, with an empty Run(). It stopped being one: each
 * row's Offset is a per-bone translation, and translation doesn't fit in the
 * retarget pose -- it can only be added during the retarget, which is here.
 *
 * **This op has to sit after FK Chains and Run IK Rig on the stack.** Ops run in
 * order and the last writer wins: with it on top, FK Chains recomputes the bone
 * afterwards and the offset is lost without a word.
 *
 * It deliberately does not implement CollectRetargetedBones. That declares
 * ownership: a bone registered there stops being parented by other ops. This op
 * does not own the weapon's bone -- FK Chains does -- it only nudges the result.
 */
USTRUCT(BlueprintType, meta = (DisplayName = "Preview Attachments (Fofuxo)"))
struct FFofuxoAttachmentsOp : public FIKRetargetOpBase
{
	GENERATED_BODY()

	FOFUXORETARGETOPS_API virtual bool Initialize(
		const FIKRetargetProcessor& InProcessor,
		const FRetargetSkeleton& InSourceSkeleton,
		const FTargetSkeleton& InTargetSkeleton,
		const FIKRetargetOpBase* InParentOp,
		FIKRigLogger& Log) override;

	FOFUXORETARGETOPS_API virtual void Run(
		FIKRetargetProcessor& InProcessor,
		const double InDeltaTime,
		const TArray<FTransform>& InSourceGlobalPose,
		TArray<FTransform>& OutTargetGlobalPose) override;

	virtual FIKRetargetOpSettingsBase* GetSettings() override { return &Settings; }

	virtual const UScriptStruct* GetSettingsType() const override
	{
		return FFofuxoAttachmentsOpSettings::StaticStruct();
	}

	virtual const UScriptStruct* GetType() const override
	{
		return FFofuxoAttachmentsOp::StaticStruct();
	}

	/** Two attachment lists in the same retargeter would be two answers to one question. */
	virtual bool IsSingleton() const override { return true; }

	UPROPERTY()
	FFofuxoAttachmentsOpSettings Settings;

private:

	/**
	 * The bone index of every row that has an offset, resolved in Initialize.
	 *
	 * Searching by name inside Run() would be one search per row per frame, and
	 * the skeleton doesn't change between one initialization and the next.
	 */
	TArray<int32> BonesWithOffset;
};
