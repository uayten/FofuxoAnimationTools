// Fofuxo -- turning a target bone with the animation running

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"

class FIKRetargetEditor;
class FIKRetargetEditorController;

struct FFofuxoAttachmentsOp;

/**
 * A rotation gizmo in Running Retarget, on the target only.
 *
 * What this solves is the hand. The retarget pose is edited while looking at the
 * ref pose, and in the ref pose the hand is open: you cannot see whether the
 * fingers close around the sword, which is the only thing that matters about
 * fingers. You only find out they are wrong when the animation plays -- and by
 * then the pose editor is no longer on.
 *
 * **What the gizmo writes is still the retarget pose, and not a fix for that
 * frame.** The retargeter has nowhere to store a per-frame correction, and this
 * mode doesn't invent one. The maths, though, makes that worth it:
 *
 * In an FK chain, a bone's output is
 *
 *     Output(B) = SourceDelta(B) * RetargetPose(B)
 *
 * -- the turn the source bone made since its own retarget pose, applied on top
 * of where the target's retarget pose puts the bone. Post-multiplying an X into
 * the retarget pose post-multiplies the same X into the output, on any frame. So
 * turning the finger here, looking at frame 37, writes the offset that produces
 * *exactly that turn* on frame 37 -- and the same turn, in world space, on all
 * the others.
 *
 * For fingers that is correct: the error of a finger gripping a sword is
 * constant, and the frame is only there so you can see it. For an error that
 * changes from frame to frame there is nothing to do here, and the answer is an
 * additive Control Rig layer in Sequencer.
 *
 * The selection comes for free: FIKRetargetDefaultMode, which is the one active
 * in Running Retarget, already picks bones by clicking in the viewport. This mode
 * joins it -- the two coexist because neither refuses the other -- and adds only
 * what was missing, which is the gizmo and the drag.
 *
 * **The source stays locked** because the gizmo only appears with the editor on
 * the target's side. There is nothing to adjust on the source: its animation is
 * the input.
 */
class FFofuxoLiveRetarget : public FEdMode
{
public:

	/** The name this mode registers itself under. */
	static const FEditorModeID Id;

	/** Turns the mode's registration on and off. The module is the one that calls. */
	static void Register();
	static void Forget();

	/** The toolbar's toggle, stored in the ini. */
	static bool IsOn();
	static void Toggle();

	/**
	 * Puts the mode on or takes it off this editor, according to the toggle and
	 * the retargeter's mode. Called from the same half-second walk that looks
	 * after the attachments.
	 */
	static void Follow(FIKRetargetEditor& Editor);

	/** Closes the walk: the undo notice is good for one lap only. */
	static void ClearUndoNotice();

	void PointAt(const TSharedPtr<FIKRetargetEditorController>& Who) { Controller = Who; }

	// FEdMode
	virtual bool UsesTransformWidget() const override;
	virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const override;
	virtual bool ShouldDrawWidget() const override;
	virtual FVector GetWidgetLocation() const override;
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& OutMatrix, void* InData) override;
	virtual bool GetCustomInputCoordinateSystem(FMatrix& OutMatrix, void* InData) override;
	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool InputKey(
		FEditorViewportClient* InViewportClient,
		FViewport* InViewport,
		FKey InKey,
		EInputEvent InEvent) override;
	virtual bool InputDelta(
		FEditorViewportClient* InViewportClient,
		FViewport* InViewport,
		FVector& InDrag,
		FRotator& InRot,
		FVector& InScale) override;
	virtual bool HandleClick(
		FEditorViewportClient* InViewportClient,
		HHitProxy* HitProxy,
		const FViewportClick& Click) override;
	virtual void Render(
		const FSceneView* View,
		FViewport* Viewport,
		FPrimitiveDrawInterface* PDI) override;
	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const override { return true; }
	// End FEdMode

private:

	/** What is selected right now, if it can be touched at all. */
	struct FChosen
	{
		FName Bone;
		FTransform InWorld;

		// The parent's, which is the frame the op's Offset is stated in. With no
		// parent -- a root bone -- it is the component's own.
		FTransform ParentInWorld;

		FQuat DeltaOnStart = FQuat::Identity;
	};

	/** The row of the attachments op that talks about the selected bone, if there is one. */
	struct FRow
	{
		FFofuxoAttachmentsOp* Op = nullptr;
		int32 Index = INDEX_NONE;
		FTransform ParentInWorld;
		FVector OnStart = FVector::ZeroVector;
	};

	/** The selected target bones, with each one's state at the start of the drag. */
	bool Gather(TArray<FChosen>& OutChosen) const;

	/**
	 * Finds the op row whose target bone is the bone selected in the viewport.
	 *
	 * That is how the move gizmo knows which row to write into: the details panel
	 * doesn't tell the edit mode who is selected in it, but the bone clicked in
	 * the viewport identifies the row with no new channel at all.
	 */
	bool FindRow(FRow& OutRow) const;

	TWeakPtr<FIKRetargetEditorController> Controller;

	TArray<FChosen> Dragging;
	FQuat Accumulated = FQuat::Identity;

	FRow Moving;
	FVector AccumulatedMove = FVector::ZeroVector;

	bool bInTransaction = false;
};
