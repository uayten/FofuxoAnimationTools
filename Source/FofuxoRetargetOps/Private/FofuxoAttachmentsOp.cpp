// Fofuxo -- the bone offset, applied during the retarget

#include "FofuxoAttachmentsOp.h"

#include "Retargeter/IKRetargetProcessor.h"

bool FFofuxoAttachmentsOp::Initialize(
	const FIKRetargetProcessor&,
	const FRetargetSkeleton&,
	const FTargetSkeleton& InTargetSkeleton,
	const FIKRetargetOpBase*,
	FIKRigLogger&)
{
	BonesWithOffset.Reset();
	BonesWithOffset.SetNum(Settings.Attachments.Num());

	for (int32 Row = 0; Row < Settings.Attachments.Num(); ++Row)
	{
		const FFofuxoAttachment& Attachment = Settings.Attachments[Row];

		// The index is resolved here even with the offset at zero: whoever has
		// the gizmo in hand moves from zero to something without reinitializing
		// anything, and a row only resolved once it already has a value would
		// never budge.
		BonesWithOffset[Row] = Attachment.Character == EFofuxoCharacter::Source
			? INDEX_NONE
			: InTargetSkeleton.FindBoneIndexByName(Attachment.TargetBone.BoneName);
	}

	bIsInitialized = true;
	return true;
}

void FFofuxoAttachmentsOp::Run(
	FIKRetargetProcessor& InProcessor,
	const double,
	const TArray<FTransform>&,
	TArray<FTransform>& OutTargetGlobalPose)
{
	const FRetargetSkeleton& Skeleton = InProcessor.GetSkeleton(ERetargetSourceOrTarget::Target);

	const int32 HowMany = FMath::Min(Settings.Attachments.Num(), BonesWithOffset.Num());

	for (int32 Row = 0; Row < HowMany; ++Row)
	{
		const int32 Index = BonesWithOffset[Row];
		const FFofuxoAttachment& Attachment = Settings.Attachments[Row];
		const FVector& Offset = Attachment.Offset;

		// bShow is read here, and not in Initialize: the checkbox has to answer
		// on the frame it is clicked, and the retargeter does not reinitialize
		// for it.
		if (Index == INDEX_NONE
			|| !Attachment.bShow
			|| Offset.IsNearlyZero()
			|| !OutTargetGlobalPose.IsValidIndex(Index))
		{
			continue;
		}

		FTransform New = OutTargetGlobalPose[Index];

		// In the *parent's* frame, and not in the bone's own. The difference
		// shows up the moment the animation turns the bone: an offset stated in
		// its own frame turns along, and then the bone describes an arc around
		// where it was before -- which is the "the hammer spins with its pivot in
		// the wrong place" complaint. Stated in the parent's frame it doesn't
		// depend on the animated rotation: the bone sits still in the new place
		// relative to the hand, and the animation turns from there, which is what
		// "I moved the bone" means.
		const int32 ParentIndex = Skeleton.GetParentIndex(Index);

		const FQuat Frame = OutTargetGlobalPose.IsValidIndex(ParentIndex)
			? OutTargetGlobalPose[ParentIndex].GetRotation()
			: FQuat::Identity;

		New.AddToTranslation(Frame.RotateVector(Offset));

		// Through the skeleton, and not writing straight into the array: the
		// bone's children have to come along, or the weapon moves and its tip
		// stays behind.
		Skeleton.SetGlobalTransformAndUpdateChildren(Index, New, OutTargetGlobalPose);
	}
}
