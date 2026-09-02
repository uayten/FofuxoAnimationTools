// Fofuxo -- hanging the preview attachments in the retarget viewport

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class FFofuxoMirrorPose;
class FIKRetargetEditor;
class UDebugSkelMeshComponent;
class USkeletalMesh;
class UIKRetargeter;
class AWorldSettings;

struct FFofuxoAttachment;

/**
 * Follows the open retarget editors and keeps the attachments hanging.
 *
 * The IK Retargeter's viewport hangs nothing off anything: it builds the preview
 * components by hand and doesn't go through FAnimationEditorPreviewScene, which
 * is what does this in Persona. This is that work, on both characters.
 *
 * The list comes from FFofuxoAttachmentsOp, which lives inside the retargeter
 * itself. There was once a second route here, reading the "Add Preview Asset"
 * entries of the USkeletalMesh and the USkeleton -- and it went for the very
 * reason that motivated the op: reimporting the rig as a new asset swaps both,
 * and everything pinned to them is left behind. The retargeter does not get
 * swapped.
 *
 * Turning it on and off belongs to the op as well: it is its Enable Op, on the
 * stack. There is no toolbar button for it, because that would be two switches
 * for one light.
 *
 * There is no event for "the retargeter swapped its preview mesh" nor for "the
 * op's list changed" that can be listened to from outside, and the editor
 * recreates the preview components when the mesh is swapped. So the state is
 * rechecked by ticker: we remember which component, which mesh and which list
 * the attachments were born from, and rebuild when any of the three changes.
 *
 * The same half-second walk is also where every other feature that needs an open
 * editor gets its turn -- there is no second walk anywhere in the plugin.
 */
class FFofuxoAttachments
{
public:

	void Start();
	void Stop();

	/** Who receives the editors the tick finds. The module is the owner. */
	void Tell(FFofuxoMirrorPose* ToMirror) { Mirror = ToMirror; }

private:

	struct FOpenEditor
	{
		TWeakObjectPtr<UIKRetargeter> Asset;

		// Where the attachments came from, so we know when to rebuild.
		TWeakObjectPtr<UDebugSkelMeshComponent> SourceComponent;
		TWeakObjectPtr<UDebugSkelMeshComponent> TargetComponent;
		TWeakObjectPtr<USkeletalMesh> SourceMesh;
		TWeakObjectPtr<USkeletalMesh> TargetMesh;

		// The op list's summary from the previous walk.
		uint32 Signature = 0;

		// These need no strong reference: the parent keeps each of them in
		// USceneComponent::AttachChildren, which is a UPROPERTY, and the parent
		// lives as long as the preview scene does. The same bet Persona makes.
		TArray<TWeakObjectPtr<USceneComponent>> Attachments;
	};

	bool Tick(float);
	void Sync(FOpenEditor& Open, FIKRetargetEditor& Editor);

	/** The list that lives in the retargeter. Each row says which character it goes on. */
	void HangFromOp(
		FOpenEditor& Open,
		const UIKRetargeter* Retargeter,
		UDebugSkelMeshComponent* Source,
		UDebugSkelMeshComponent* Target);

	/** One row of the list, on one character. */
	void HangOn(
		FOpenEditor& Open,
		UDebugSkelMeshComponent* Component,
		UObject* Object,
		const FFofuxoAttachment& Attachment,
		FName Bone);

	void Attach(
		FOpenEditor& Open,
		UDebugSkelMeshComponent* Component,
		AWorldSettings* Owner,
		UObject* Object,
		FName Socket,
		const FTransform& Fit = FTransform::Identity);

	void Release(FOpenEditor& Open);

	TArray<FOpenEditor> Open;
	FTSTicker::FDelegateHandle Ticker;
	FFofuxoMirrorPose* Mirror = nullptr;
};
