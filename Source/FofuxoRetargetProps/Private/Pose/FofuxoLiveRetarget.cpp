// Fofuxo -- turning a target bone with the animation running

#include "FofuxoLiveRetarget.h"

#include "FofuxoAttachmentsOp.h"
#include "FofuxoBonesOnScreen.h"
#include "FofuxoResetRotation.h"

#include "Animation/DebugSkelMeshComponent.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorModeRegistry.h"
#include "EditorUndoClient.h"
#include "EditorViewportClient.h"
#include "Framework/Commands/UICommandList.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/ConfigCacheIni.h"
#include "ReferenceSkeleton.h"
#include "RetargetEditor/IKRetargetCommands.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoLive, Log, All);

const FEditorModeID FFofuxoLiveRetarget::Id("FofuxoLiveRetarget");

namespace FofuxoLive
{
	static const TCHAR* IniSection = TEXT("FofuxoRetargetProps");
	static const TCHAR* IniKey = TEXT("LiveRetarget");

	static bool bReadFromIni = false;
	static bool bOn = false;

	/**
	 * Knows that a Ctrl+Z has just happened.
	 *
	 * The retargeter's PostUndo rebuilds the preview meshes, reinitializes the
	 * processor and touches playback, and along that path the editor sometimes
	 * falls back into Editing Retarget Pose with the toggle still on. There is no
	 * fixing it from the inside: SetRetargeterMode and all its neighbours belong
	 * to IKRigEditor and are not exported. What is left is the toolbar's command,
	 * which is public -- but it may only be fired when the mode changed *on its
	 * own*, and not when you were the one who switched it.
	 */
	class FUndoWatcher : public FSelfRegisteringEditorUndoClient
	{
	public:
		virtual void PostUndo(bool) override { bHappened = true; }
		virtual void PostRedo(bool) override { bHappened = true; }

		bool bHappened = false;
	};

	static TUniquePtr<FUndoWatcher> Watcher;

	/** Which mode each retargeter was in on the previous walk. */
	static TMap<TWeakObjectPtr<UIKRetargeter>, uint8> PreviousMode;
}

void FFofuxoLiveRetarget::Register()
{
	// bVisible is false: this mode is not meant to show up in the level editor's
	// mode bar, it only exists inside the retarget editor. It is the same thing
	// IKRigEditor does with its own two modes.
	FofuxoLive::Watcher = MakeUnique<FofuxoLive::FUndoWatcher>();

	FEditorModeRegistry::Get().RegisterMode<FFofuxoLiveRetarget>(
		Id,
		LOCTEXT("LiveRetargetMode", "Live Retarget"),
		FSlateIcon(),
		/*bVisible*/ false);
}

void FFofuxoLiveRetarget::Forget()
{
	FEditorModeRegistry::Get().UnregisterMode(Id);

	FofuxoLive::Watcher.Reset();
	FofuxoLive::PreviousMode.Reset();
}

bool FFofuxoLiveRetarget::IsOn()
{
	if (!FofuxoLive::bReadFromIni)
	{
		FofuxoLive::bReadFromIni = true;
		GConfig->GetBool(FofuxoLive::IniSection, FofuxoLive::IniKey,
			FofuxoLive::bOn, GEditorPerProjectIni);
	}

	return FofuxoLive::bOn;
}

void FFofuxoLiveRetarget::Toggle()
{
	// The lazy read first: without this the next query after this one would go to
	// the ini and undo the choice.
	IsOn();

	FofuxoLive::bOn = !FofuxoLive::bOn;

	GConfig->SetBool(FofuxoLive::IniSection, FofuxoLive::IniKey,
		FofuxoLive::bOn, GEditorPerProjectIni);
}

void FFofuxoLiveRetarget::ClearUndoNotice()
{
	if (FofuxoLive::Watcher.IsValid())
	{
		FofuxoLive::Watcher->bHappened = false;
	}

	// A closed retargeter drops out of the count -- otherwise the map grows for
	// the whole session.
	for (auto Step = FofuxoLive::PreviousMode.CreateIterator(); Step; ++Step)
	{
		if (!Step.Key().IsValid())
		{
			Step.RemoveCurrent();
		}
	}
}

void FFofuxoLiveRetarget::Follow(FIKRetargetEditor& Editor)
{
	const TSharedRef<FIKRetargetEditorController> Controller = Editor.GetController();

	UIKRetargeter* Asset = Controller->AssetController != nullptr
		? Controller->AssetController->GetAsset()
		: nullptr;

	ERetargeterOutputMode Mode = Controller->GetRetargeterMode();

	// Only go back when the mode changed on its own: if you clicked Editing
	// Retarget Pose and then hit Ctrl+Z, the switch was yours and it stands.
	const uint8* Before = Asset != nullptr ? FofuxoLive::PreviousMode.Find(Asset) : nullptr;

	const bool bFellOnItsOwn = FofuxoLive::Watcher.IsValid()
		&& FofuxoLive::Watcher->bHappened
		&& IsOn()
		&& Mode == ERetargeterOutputMode::EditRetargetPose
		&& Before != nullptr
		&& static_cast<ERetargeterOutputMode>(*Before) == ERetargeterOutputMode::RunRetarget;

	if (bFellOnItsOwn)
	{
		// Through the toolbar's command, which is the only public route to
		// SetRetargeterMode.
		Editor.GetToolkitCommands()->TryExecuteAction(
			FIKRetargetCommands::Get().RunRetargeter.ToSharedRef());

		Mode = Controller->GetRetargeterMode();
	}

	if (Asset != nullptr)
	{
		FofuxoLive::PreviousMode.Add(Asset, static_cast<uint8>(Mode));
	}

	// The mode goes in whenever the editor is open, and not only in Live Retarget:
	// the one that draws the sticks and the one that catches the click by
	// proximity is this mode, and both of those apply in either retargeter mode.
	//
	// The gizmo, that one stays in Live Retarget only -- what holds it back is
	// Gather(), which refuses outside it. In Editing Retarget Pose the engine's
	// own gizmo rules, and two gizmos on the same screen would be two answers to
	// one drag.
	constexpr bool bWanted = true;

	FEditorModeTools& Modes = Editor.GetEditorModeManager();
	const bool bIsOn = Modes.IsModeActive(Id);

	if (bWanted == bIsOn)
	{
		return;
	}

	if (!bWanted)
	{
		Modes.DeactivateMode(Id);
		return;
	}

	Modes.ActivateMode(Id);

	if (FFofuxoLiveRetarget* Mine = Modes.GetActiveModeTyped<FFofuxoLiveRetarget>(Id))
	{
		Mine->PointAt(Controller.ToSharedPtr());
	}
}

bool FFofuxoLiveRetarget::Gather(TArray<FChosen>& OutChosen) const
{
	const TSharedPtr<FIKRetargetEditorController> Who = Controller.Pin();
	if (!Who.IsValid())
	{
		return false;
	}

	// The mode stays active the whole time, because of the drawing and the click.
	// The gizmo doesn't: outside Live Retarget the engine's gizmo rules. This is
	// the only place that has to say so -- the whole gizmo path goes through here.
	if (!IsOn() || Who->GetRetargeterMode() != ERetargeterOutputMode::RunRetarget)
	{
		return false;
	}

	// Target only. The source's animation is the input -- there is nothing to
	// adjust in it, and letting the gizmo show up there would be an invitation to
	// ruin the reference.
	if (Who->GetSourceOrTarget() != ERetargetSourceOrTarget::Target)
	{
		return false;
	}

	UIKRetargeterController* AssetController = Who->AssetController;
	UDebugSkelMeshComponent* Component = Who->TargetSkelMeshComponent;

	if (AssetController == nullptr || Component == nullptr)
	{
		return false;
	}

	USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset();
	if (Mesh == nullptr)
	{
		return false;
	}

	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();

	const TMap<FName, FQuat>& Deltas =
		AssetController->GetCurrentRetargetPose(ERetargetSourceOrTarget::Target).GetAllDeltaRotations();

	for (const FName& Bone : Who->GetSelectedBones())
	{
		const int32 Index = Skeleton.FindBoneIndex(Bone);
		if (Index == INDEX_NONE)
		{
			continue;
		}

		FChosen Chosen;
		Chosen.Bone = Bone;

		// In world space, which is where the gizmo works. The component's rotation
		// cancels out in the drag's sum, so there is no error in mixing the two.
		Chosen.InWorld = Component->GetBoneTransform(Index);

		// The parent's goes along because it is the frame the op's Offset is
		// stated in -- the drag has to be converted into the same frame Run() will
		// read it back in.
		const int32 ParentIndex = Skeleton.GetParentIndex(Index);

		Chosen.ParentInWorld = ParentIndex == INDEX_NONE
			? Component->GetComponentTransform()
			: Component->GetBoneTransform(ParentIndex);

		if (const FQuat* Found = Deltas.Find(Bone))
		{
			Chosen.DeltaOnStart = *Found;
		}

		OutChosen.Add(Chosen);
	}

	return !OutChosen.IsEmpty();
}

bool FFofuxoLiveRetarget::FindRow(FRow& OutRow) const
{
	TArray<FChosen> Chosen;
	if (!Gather(Chosen))
	{
		return false;
	}

	const TSharedPtr<FIKRetargetEditorController> Who = Controller.Pin();
	UIKRetargeter* Asset = Who.IsValid() && Who->AssetController != nullptr
		? Who->AssetController->GetAsset()
		: nullptr;

	if (Asset == nullptr)
	{
		return false;
	}

	FFofuxoAttachmentsOp* Op = Asset->GetFirstRetargetOpOfType<FFofuxoAttachmentsOp>();
	if (Op == nullptr)
	{
		return false;
	}

	// By the last clicked one, which is the same bone the gizmo is drawn on.
	const FChosen& Last = Chosen.Last();

	for (int32 Row = 0; Row < Op->Settings.Attachments.Num(); ++Row)
	{
		const FFofuxoAttachment& Attachment = Op->Settings.Attachments[Row];

		// bShow comes in here for the same reason it comes into Run(): a row that
		// is off doesn't act. Without this, with two rows on the same bone, the
		// gizmo would always write into the first -- including when the one in
		// sight is the second.
		if (!Attachment.bShow
			|| Attachment.Character == EFofuxoCharacter::Source
			|| Attachment.TargetBone.BoneName != Last.Bone)
		{
			continue;
		}

		OutRow.Op = Op;
		OutRow.Index = Row;
		OutRow.ParentInWorld = Last.ParentInWorld;
		OutRow.OnStart = Attachment.Offset;

		return true;
	}

	return false;
}

bool FFofuxoLiveRetarget::HandleClick(
	FEditorViewportClient* InViewportClient,
	HHitProxy* HitProxy,
	const FViewportClick& Click)
{
	if (InViewportClient == nullptr || Click.GetKey() != EKeys::LeftMouseButton)
	{
		return false;
	}

	// It hit the bone dead on: the engine's mode has already selected it, and a
	// second search could only disagree with it.
	if (FFofuxoBonesOnScreen::IsBoneProxy(HitProxy))
	{
		return false;
	}

	FViewport* Viewport = InViewportClient->Viewport;
	if (Viewport == nullptr)
	{
		return false;
	}

	HHitProxy* Near = FFofuxoBonesOnScreen::BoneNearCursor(
		*Viewport, Viewport->GetMouseX(), Viewport->GetMouseY());

	if (Near == nullptr)
	{
		return false;
	}

	FEditorModeTools* Modes = GetModeManager();
	if (Modes == nullptr)
	{
		return false;
	}

	// The one that selects is still the engine's mode: we hand it the proxy the
	// click missed by a hair, and it does the rest -- selection, details panel,
	// hierarchy. The call is virtual, so it needs no exported symbol from
	// IKRigEditor at all; and since its mode runs before ours and has already
	// cleared the selection, writing now is the last word.
	for (const FEditorModeID& Theirs : {
		FEditorModeID("IKRetargetAssetDefaultMode"),
		FEditorModeID("IKRetargetAssetEditMode")})
	{
		if (FEdMode* Mode = Modes->GetActiveMode(Theirs))
		{
			return Mode->HandleClick(InViewportClient, Near, Click);
		}
	}

	return false;
}

void FFofuxoLiveRetarget::Render(
	const FSceneView* View,
	FViewport* Viewport,
	FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);

	if (const TSharedPtr<FIKRetargetEditorController> Who = Controller.Pin())
	{
		FFofuxoBonesOnScreen::Draw(*Who, View, PDI);
	}
}

bool FFofuxoLiveRetarget::UsesTransformWidget() const
{
	TArray<FChosen> Chosen;
	return Gather(Chosen);
}

bool FFofuxoLiveRetarget::UsesTransformWidget(const UE::Widget::EWidgetMode CheckMode) const
{
	if (CheckMode == UE::Widget::WM_Rotate)
	{
		// Rotation goes into the retarget pose, and applies to any target bone.
		return UsesTransformWidget();
	}

	if (CheckMode == UE::Widget::WM_Translate)
	{
		// Moving only applies where there is somewhere to store it: the retarget
		// pose has no per-bone translation, so the destination is the Offset of a
		// row of the attachments op, and the gizmo only shows up on a bone some
		// row names.
		FRow Which;
		return FindRow(Which);
	}

	return false;
}

bool FFofuxoLiveRetarget::ShouldDrawWidget() const
{
	return UsesTransformWidget();
}

FVector FFofuxoLiveRetarget::GetWidgetLocation() const
{
	TArray<FChosen> Chosen;
	if (!Gather(Chosen))
	{
		return FVector::ZeroVector;
	}

	// On the last clicked one, as the engine does: the selection list grows at the
	// end.
	return Chosen.Last().InWorld.GetLocation();
}

bool FFofuxoLiveRetarget::GetCustomDrawingCoordinateSystem(FMatrix& OutMatrix, void*)
{
	TArray<FChosen> Chosen;
	if (!Gather(Chosen))
	{
		return false;
	}

	OutMatrix = FRotationMatrix::Make(Chosen.Last().InWorld.GetRotation());
	return true;
}

bool FFofuxoLiveRetarget::GetCustomInputCoordinateSystem(FMatrix& OutMatrix, void* InData)
{
	return GetCustomDrawingCoordinateSystem(OutMatrix, InData);
}

bool FFofuxoLiveRetarget::StartTracking(FEditorViewportClient* InViewportClient, FViewport*)
{
	Dragging.Reset();
	Accumulated = FQuat::Identity;
	Moving = FRow();
	AccumulatedMove = FVector::ZeroVector;

	// With no gizmo axis grabbed, this drag isn't ours -- it's the camera. Saying
	// yes here is what used to swallow alt+click, the middle button and the right
	// button: StartTracking is called for *every* drag in the viewport, and
	// returning true says "I am manipulating something, don't move the camera".
	if (InViewportClient == nullptr || InViewportClient->GetCurrentWidgetAxis() == EAxisList::None)
	{
		return false;
	}

	const bool bMoving = InViewportClient->GetWidgetMode() == UE::Widget::WM_Translate;

	if (bMoving ? !FindRow(Moving) : !Gather(Dragging))
	{
		return false;
	}

	const TSharedPtr<FIKRetargetEditorController> Who = Controller.Pin();
	UIKRetargeter* Asset = Who.IsValid() && Who->AssetController != nullptr
		? Who->AssetController->GetAsset()
		: nullptr;

	if (Asset == nullptr)
	{
		Dragging.Reset();
		Moving = FRow();
		return false;
	}

	// One transaction for the whole drag, opened by hand rather than through
	// FScopedTransaction: it starts here and only closes in EndTracking.
	GEditor->BeginTransaction(bMoving
		? LOCTEXT("MoveLiveTransaction", "Move a bone with the animation running")
		: LOCTEXT("LiveRetargetTransaction", "Turn a bone with the animation running"));
	Asset->Modify();
	bInTransaction = true;

	return true;
}

bool FFofuxoLiveRetarget::InputKey(
	FEditorViewportClient* InViewportClient,
	FViewport* InViewport,
	FKey InKey,
	EInputEvent InEvent)
{
	// The viewport's hit proxy buffer is cached: it is only redrawn when someone
	// invalidates it -- moving the camera, switching gizmo mode, changing the
	// selection from the hierarchy. A running animation invalidates nothing; it
	// repaints the image and that is all. So in Running Retarget the buffer
	// freezes on the pose of the instant it was made, and the click selects the
	// bone that was in that pixel *at that instant* -- the gizmo shows up far from
	// where you clicked, and only closing and reopening the retargeter puts it
	// right. It applies to the dead-on click, which is the engine's, and to the
	// proximity search in HandleClick here: both read this buffer.
	//
	// Invalidating on the button press arrives in time. FEditorViewportClient
	// hands the key to the modes *before* resolving the click's proxy and before
	// deciding whether the gizmo was grabbed, so what answers both questions is a
	// buffer made just now. The cost is one hit proxy draw per click, which is the
	// same thing moving the camera already charged.
	if (InEvent == IE_Pressed && InKey == EKeys::LeftMouseButton && InViewport != nullptr)
	{
		const TSharedPtr<FIKRetargetEditorController> Who = Controller.Pin();

		if (Who.IsValid() && Who->GetRetargeterMode() == ERetargeterOutputMode::RunRetarget)
		{
			InViewport->InvalidateHitProxy();
		}
	}

	// Alt+R is also on the toolkit's command list, which is what makes it work
	// with the focus on the hierarchy or the op stack. Here it is caught earlier,
	// in the viewport, because FEditorModeTools sees the key before it bubbles up
	// -- without this, the same shortcut would work everywhere except where you
	// are looking.
	if (InEvent == IE_Pressed
		&& InKey == EKeys::R
		&& InViewport != nullptr
		&& (InViewport->KeyState(EKeys::LeftAlt) || InViewport->KeyState(EKeys::RightAlt)))
	{
		if (FFofuxoResetRotation::Can(Controller))
		{
			FFofuxoResetRotation::Reset(Controller);
			return true;
		}
	}

	return FEdMode::InputKey(InViewportClient, InViewport, InKey, InEvent);
}

bool FFofuxoLiveRetarget::EndTracking(FEditorViewportClient*, FViewport*)
{
	if (bInTransaction)
	{
		GEditor->EndTransaction();
		bInTransaction = false;
	}

	const bool bTouched = !Dragging.IsEmpty() || Moving.Op != nullptr;

	Dragging.Reset();
	Accumulated = FQuat::Identity;
	Moving = FRow();
	AccumulatedMove = FVector::ZeroVector;

	return bTouched;
}

bool FFofuxoLiveRetarget::InputDelta(
	FEditorViewportClient* InViewportClient,
	FViewport*,
	FVector& InDrag,
	FRotator& InRot,
	FVector&)
{
	if (Moving.Op != nullptr && InViewportClient->GetWidgetMode() == UE::Widget::WM_Translate)
	{
		AccumulatedMove += InDrag;

		if (!Moving.Op->Settings.Attachments.IsValidIndex(Moving.Index))
		{
			return false;
		}

		// The drag arrives in world space, and the Offset is stated in the bone's
		// parent's frame -- that is how the op's Run() will read it back.
		const FVector InParent = Moving.ParentInWorld.GetRotation().UnrotateVector(AccumulatedMove);

		FFofuxoAttachmentsOp* Op = Moving.Op;
		Op->Settings.Attachments[Moving.Index].Offset = Moving.OnStart + InParent;

#if WITH_EDITORONLY_DATA
		// The copy running in the viewport is another one: without this the value
		// would only show up on the retargeter's next reinitialization, and the
		// drag would be blind.
		if (FIKRetargetOpSettingsBase* Running = Op->Settings.EditorInstance)
		{
			Running->CopySettingsAtRuntime(&Op->Settings);
		}
#endif

		return true;
	}

	if (Dragging.IsEmpty() || InViewportClient->GetWidgetMode() != UE::Widget::WM_Rotate)
	{
		return false;
	}

	const TSharedPtr<FIKRetargetEditorController> Who = Controller.Pin();
	if (!Who.IsValid() || Who->AssetController == nullptr)
	{
		return false;
	}

	// The turn accumulated since the start of the drag, in world space.
	Accumulated = InRot.Quaternion() * Accumulated;

	const FVector Axis = Accumulated.GetRotationAxis();
	const float Angle = Accumulated.GetAngle();

	for (const FChosen& Chosen : Dragging)
	{
		// The same turn, stated in the bone's frame. It is the conjugation of the
		// world turn by the bone's transform -- and it is because of that that
		// post-multiplying this into the retarget pose produces, in the output,
		// exactly the turn you made on screen.
		const FVector InBone = Chosen.InWorld.InverseTransformVector(Axis);
		const FQuat New = (Chosen.DeltaOnStart * FQuat(InBone, Angle)).GetNormalized();

		Who->AssetController->SetRotationOffsetForRetargetPoseBone(
			Chosen.Bone, New, ERetargetSourceOrTarget::Target);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
