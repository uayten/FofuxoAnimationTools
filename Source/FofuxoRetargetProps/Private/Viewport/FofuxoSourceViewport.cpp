// Fofuxo -- a second viewport window, locked to the source bone

#include "FofuxoSourceViewport.h"

#include "Algo/Reverse.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "EditorViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Docking/WorkspaceItem.h"
#include "IPersonaPreviewScene.h"
#include "IPersonaToolkit.h"
#include "PreviewScene.h"
#include "ReferenceSkeleton.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargetChainMapping.h"
#include "Retargeter/IKRetargetOps.h"
#include "Retargeter/IKRetargeter.h"
#include "Rig/IKRigDefinition.h"
#include "SEditorViewport.h"
#include "StructUtils/InstancedStruct.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

const FName FFofuxoSourceViewport::TabId("FofuxoSourceViewport");

namespace FofuxoSourceViewport
{
	/** The tab managers we have put the registration on, to take it off at shutdown. */
	static TArray<TWeakPtr<FTabManager>> Registered;

	/**
	 * A chain's bones, from start to end.
	 *
	 * A chain is defined by two names, and the path between them only exists
	 * bottom-up: a bone has a parent, not a list of children. So we climb from the
	 * end until we find the start, and the result is reversed.
	 *
	 * Returns false if the end doesn't descend from the start -- a badly defined
	 * chain, or names that don't belong to this skeleton.
	 */
	static bool BonesOfChain(
		const FReferenceSkeleton& Skeleton,
		const FName Start,
		const FName End,
		TArray<FName>& OutBones)
	{
		const int32 StartIndex = Skeleton.FindBoneIndex(Start);
		const int32 EndIndex = Skeleton.FindBoneIndex(End);

		if (StartIndex == INDEX_NONE || EndIndex == INDEX_NONE)
		{
			return false;
		}

		TArray<FName> BackToFront;

		int32 Walking = EndIndex;
		while (Walking != INDEX_NONE)
		{
			BackToFront.Add(Skeleton.GetBoneName(Walking));

			if (Walking == StartIndex)
			{
				break;
			}

			Walking = Skeleton.GetParentIndex(Walking);
		}

		if (Walking != StartIndex)
		{
			return false;
		}

		Algo::Reverse(BackToFront);
		OutBones = MoveTemp(BackToFront);

		return true;
	}

	/** This retargeter's chain mapping, which lives in the first op that has one. */
	static const FRetargetChainMapping* MappingOf(const UIKRetargeter* Retargeter)
	{
		if (Retargeter == nullptr)
		{
			return nullptr;
		}

		for (const FInstancedStruct& Op : Retargeter->GetRetargetOps())
		{
			if (const FIKRetargetOpBase* Base = Op.GetPtr<FIKRetargetOpBase>())
			{
				if (const FRetargetChainMapping* Mapping = Base->GetChainMapping())
				{
					return Mapping;
				}
			}
		}

		return nullptr;
	}

	/**
	 * The source bone corresponding to a target bone.
	 *
	 * Through the chain mapping, which is the only correspondence the retargeter
	 * actually knows: find the target chain containing the bone, and the
	 * counterpart is the bone at the same proportional position in the mapped
	 * chain. A three-bone hand mapped onto a four-bone one has no exact answer,
	 * and the proportion is the least wrong one.
	 *
	 * With no chain that serves, it tries the same name on the source -- which
	 * works between skeletons following Unreal's convention -- and then the
	 * pelvis, which at least puts the camera on the right character.
	 */
	static FName CounterpartOnSource(
		const FIKRetargetEditorController& Who,
		const FName TargetBone,
		const FReferenceSkeleton& SourceSkeleton)
	{
		UIKRetargeterController* Control = Who.AssetController;
		if (Control == nullptr || TargetBone.IsNone())
		{
			return NAME_None;
		}

		// If you are editing the source, the selected bone is already the source's.
		if (Who.GetSourceOrTarget() == ERetargetSourceOrTarget::Source)
		{
			return TargetBone;
		}

		const UDebugSkelMeshComponent* Target = Who.TargetSkelMeshComponent;
		const USkeletalMesh* TargetMesh = Target != nullptr ? Target->GetSkeletalMeshAsset() : nullptr;

		const UIKRigDefinition* TargetRig = Control->GetIKRig(ERetargetSourceOrTarget::Target);
		const UIKRigDefinition* SourceRig = Control->GetIKRig(ERetargetSourceOrTarget::Source);
		const FRetargetChainMapping* Mapping = MappingOf(Control->GetAsset());

		if (TargetMesh != nullptr && TargetRig != nullptr && SourceRig != nullptr && Mapping != nullptr)
		{
			const FReferenceSkeleton& TargetSkeleton = TargetMesh->GetRefSkeleton();

			for (const FBoneChain& Chain : TargetRig->GetRetargetChains())
			{
				TArray<FName> TargetBones;
				if (!BonesOfChain(TargetSkeleton, Chain.StartBone.BoneName, Chain.EndBone.BoneName, TargetBones))
				{
					continue;
				}

				const int32 Where = TargetBones.IndexOfByKey(TargetBone);
				if (Where == INDEX_NONE)
				{
					continue;
				}

				const FName NameOnSource =
					Mapping->GetChainMappedTo(Chain.ChainName, ERetargetSourceOrTarget::Target);

				if (NameOnSource.IsNone())
				{
					break;
				}

				for (const FBoneChain& Other : SourceRig->GetRetargetChains())
				{
					if (Other.ChainName != NameOnSource)
					{
						continue;
					}

					TArray<FName> SourceBones;
					if (!BonesOfChain(SourceSkeleton, Other.StartBone.BoneName, Other.EndBone.BoneName, SourceBones)
						|| SourceBones.IsEmpty())
					{
						break;
					}

					if (TargetBones.Num() <= 1 || SourceBones.Num() <= 1)
					{
						return SourceBones[0];
					}

					const float Fraction = static_cast<float>(Where) / static_cast<float>(TargetBones.Num() - 1);
					const int32 Chosen = FMath::Clamp(
						FMath::RoundToInt(Fraction * static_cast<float>(SourceBones.Num() - 1)),
						0,
						SourceBones.Num() - 1);

					return SourceBones[Chosen];
				}

				break;
			}
		}

		// Same name: between two skeletons following Unreal's convention this hits
		// dead on, and it costs nothing to try.
		if (SourceSkeleton.FindBoneIndex(TargetBone) != INDEX_NONE)
		{
			return TargetBone;
		}

		const FName Pelvis = Control->GetPelvisBone(ERetargetSourceOrTarget::Source);
		return SourceSkeleton.FindBoneIndex(Pelvis) != INDEX_NONE ? Pelvis : NAME_None;
	}
}

/**
 * This viewport's camera: it follows, frame by frame, the source bone matching
 * the bone selected in the main viewport.
 */
class FFofuxoSourceCamera : public FEditorViewportClient
{
public:

	FFofuxoSourceCamera(
		FPreviewScene* Scene,
		const TSharedRef<SEditorViewport>& Viewport,
		const TWeakPtr<FIKRetargetEditorController>& Who)
		: FEditorViewportClient(nullptr, Scene, Viewport)
		, Controller(Who)
	{
		SetViewportType(LVT_Perspective);
		SetViewMode(VMI_Lit);

		// Without this the animation would only move when something asked for a
		// redraw, and the viewport would sit frozen on one frame while the main one
		// runs.
		SetRealtime(true);

		// A viewport for looking: there is no selection of its own here, and the
		// main viewport's selection outline drawn here would only confuse.
		EngineShowFlags.SetSelectionOutline(false);
	}

	virtual void Tick(float DeltaSeconds) override
	{
		FEditorViewportClient::Tick(DeltaSeconds);
		Follow();
	}

private:

	void Follow()
	{
		const TSharedPtr<FIKRetargetEditorController> Who = Controller.Pin();
		if (!Who.IsValid())
		{
			return;
		}

		UDebugSkelMeshComponent* Source = Who->SourceSkelMeshComponent;
		USkeletalMesh* Mesh = Source != nullptr ? Source->GetSkeletalMeshAsset() : nullptr;

		if (Mesh == nullptr)
		{
			return;
		}

		const TArray<FName>& Selected = Who->GetSelectedBones();
		const FName Clicked = Selected.IsEmpty() ? NAME_None : Selected.Last();

		// The chain sum is only redone when the selection changes: it sweeps both
		// rigs' chains, and doing that at sixty frames a second would be waste.
		if (Clicked != LastClicked)
		{
			LastClicked = Clicked;
			SourceBone = FofuxoSourceViewport::CounterpartOnSource(*Who, Clicked, Mesh->GetRefSkeleton());
			bNeedsFraming = true;
		}

		if (SourceBone.IsNone())
		{
			return;
		}

		const int32 Index = Mesh->GetRefSkeleton().FindBoneIndex(SourceBone);
		if (Index == INDEX_NONE)
		{
			return;
		}

		const FVector Where = Source->GetBoneTransform(Index).GetLocation();

		if (bNeedsFraming && !bHasFramed)
		{
			// Only the very first time. From then on the distance and the orbit are
			// the ones you left -- reframing on every bone would undo the zoom you
			// have just made to see the finger.
			bHasFramed = true;
			FocusViewportOnBox(FBox::BuildAABB(Where, FVector(BoneSize(*Source, Index))), true);
		}
		else
		{
			// The distance comes from the current state, and not from a constant:
			// that way the mouse wheel is still the zoom, and we only touch the
			// centre.
			const double Distance = FMath::Max((GetViewLocation() - GetLookAtLocation()).Size(), 5.0);
			SetViewLocationForOrbiting(Where, static_cast<float>(Distance));
		}

		bNeedsFraming = false;
	}

	/** A plausible radius to frame with: the bone's own length, with a floor. */
	static float BoneSize(const USkeletalMeshComponent& Component, const int32 Index)
	{
		const FReferenceSkeleton& Skeleton = Component.GetSkeletalMeshAsset()->GetRefSkeleton();
		const int32 Parent = Skeleton.GetParentIndex(Index);

		if (Parent == INDEX_NONE)
		{
			return 100.f;
		}

		const float Length = static_cast<float>(FVector::Dist(
			Component.GetBoneTransform(Index).GetLocation(),
			Component.GetBoneTransform(Parent).GetLocation()));

		return FMath::Clamp(Length * 3.f, 5.f, 200.f);
	}

	TWeakPtr<FIKRetargetEditorController> Controller;

	FName LastClicked = NAME_None;
	FName SourceBone = NAME_None;

	bool bNeedsFraming = false;
	bool bHasFramed = false;
};

/** The tab's widget: a bare viewport over the editor's preview scene. */
class SFofuxoSourceViewport : public SEditorViewport
{
public:

	SLATE_BEGIN_ARGS(SFofuxoSourceViewport) {}
	SLATE_END_ARGS()

	void Construct(
		const FArguments& InArgs,
		const TWeakPtr<FIKRetargetEditorController>& InController,
		const TSharedPtr<IPersonaPreviewScene>& InScene)
	{
		Controller = InController;

		// Kept as a strong reference on purpose: the viewport client holds a raw
		// pointer to the scene, and the destruction order at editor close is not
		// ours to guarantee.
		Scene = InScene;

		SEditorViewport::Construct(SEditorViewport::FArguments());
	}

	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override
	{
		return MakeShared<FFofuxoSourceCamera>(
			Scene.IsValid() ? static_cast<FPreviewScene*>(Scene.Get()) : nullptr,
			SharedThis(this),
			Controller);
	}

private:

	TWeakPtr<FIKRetargetEditorController> Controller;
	TSharedPtr<IPersonaPreviewScene> Scene;
};

void FFofuxoSourceViewport::EnsureTab(FIKRetargetEditor& Editor)
{
	const TSharedPtr<FTabManager> Tabs = Editor.GetTabManager();
	if (!Tabs.IsValid())
	{
		return;
	}

	// This walk happens twice a second.
	if (Tabs->HasTabSpawner(TabId))
	{
		return;
	}

	const TWeakPtr<FIKRetargetEditorController> Weak = Editor.GetController();
	const TSharedPtr<IPersonaPreviewScene> Scene = Editor.GetPersonaToolkit()->GetPreviewScene();

	Tabs->RegisterTabSpawner(TabId, FOnSpawnTab::CreateLambda(
		[Weak, Scene](const FSpawnTabArgs&) -> TSharedRef<SDockTab>
		{
			return SNew(SDockTab)
				.Label(LOCTEXT("SourceViewportTab", "Source (Fofuxo)"))
				[
					SNew(SFofuxoSourceViewport, Weak, Scene)
				];
		}))
		.SetDisplayName(LOCTEXT("SourceViewportTab", "Source (Fofuxo)"))
		.SetTooltipText(LOCTEXT("SourceViewportTabTip",
			"A second viewport onto the same scene, with the camera locked to the source "
			"bone matching the selected bone. It is for seeing the reference and the "
			"adjustment at once, without flying the camera between the two characters."))
		.SetGroup(Tabs->GetLocalWorkspaceMenuRoot())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));

	FofuxoSourceViewport::Registered.AddUnique(Tabs);
}

void FFofuxoSourceViewport::Open(FIKRetargetEditor& Editor)
{
	EnsureTab(Editor);

	if (const TSharedPtr<FTabManager> Tabs = Editor.GetTabManager())
	{
		Tabs->TryInvokeTab(FTabId(TabId));
	}
}

void FFofuxoSourceViewport::Forget()
{
	for (const TWeakPtr<FTabManager>& Weak : FofuxoSourceViewport::Registered)
	{
		const TSharedPtr<FTabManager> Tabs = Weak.Pin();
		if (!Tabs.IsValid())
		{
			continue;
		}

		// Close before unregistering: the widget inside it belongs to this DLL.
		if (const TSharedPtr<SDockTab> Tab = Tabs->FindExistingLiveTab(FTabId(TabId)))
		{
			Tab->RequestCloseTab();
		}

		Tabs->UnregisterTabSpawner(TabId);
	}

	FofuxoSourceViewport::Registered.Reset();
}

#undef LOCTEXT_NAMESPACE
