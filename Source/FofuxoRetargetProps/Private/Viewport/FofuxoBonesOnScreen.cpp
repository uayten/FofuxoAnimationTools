// Fofuxo -- drawing and hitting bones in the retarget viewport

#include "FofuxoBonesOnScreen.h"

#include "Animation/DebugSkelMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "HitProxies.h"
#include "Misc/ConfigCacheIni.h"
#include "Preferences/PersonaOptions.h"
#include "PrimitiveDrawingUtils.h"
#include "ReferenceSkeleton.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "UnrealClient.h"

namespace FofuxoBones
{
	static const TCHAR* IniSection = TEXT("FofuxoRetargetProps");
	static const TCHAR* IniKey = TEXT("StickBones");

	static bool bReadFromIni = false;
	static bool bOn = false;

	/** The previous BoneDrawSize, per retargeter we touched. */
	static TMap<TWeakObjectPtr<UIKRetargeter>, float> Stored;

	/**
	 * The size the engine's drawing shrinks to.
	 *
	 * Not zero: the bone's hit proxy is the drawing, and a bone of size zero would
	 * not be clickable even by the proximity search, which reads the same buffer.
	 */
	static constexpr float ShrunkenSize = 0.15f;

	/**
	 * The click's search radius, in pixels.
	 *
	 * Generous on purpose, because the nearest one wins: a large radius doesn't
	 * pick the wrong bone, it only stops demanding aim. What it costs is that
	 * clicking on empty space within 22 pixels of a bone still selects that bone.
	 */
	static constexpr int32 RadiusInPixels = 22;

	/** The name of the proxy type IKRigEditor puts on each bone. */
	static const TCHAR* ProxyName = TEXT("HIKRetargetEditorBoneProxy");

	/** The joint's circle and the stick's thickness, in pixels. */
	static constexpr float JointRadius = 3.2f;
	static constexpr float StickThickness = 1.6f;

	/** One character, ready to draw. */
	struct FCharacterToDraw
	{
		USkeletalMesh* Mesh = nullptr;
		TArray<FVector> Where;

		bool Serves() const { return Mesh != nullptr && Where.Num() > 0; }
		const FReferenceSkeleton& Skeleton() const { return Mesh->GetRefSkeleton(); }
	};

	static void Gather(const FIKRetargetEditorController& Who, const ERetargetSourceOrTarget Side, FCharacterToDraw& Out)
	{
		UDebugSkelMeshComponent* Component = Side == ERetargetSourceOrTarget::Source
			? Who.SourceSkelMeshComponent
			: Who.TargetSkelMeshComponent;

		if (Component == nullptr)
		{
			return;
		}

		Out.Mesh = Component->GetSkeletalMeshAsset();
		if (Out.Mesh == nullptr)
		{
			return;
		}

		const int32 HowMany = Out.Mesh->GetRefSkeleton().GetNum();
		Out.Where.Reserve(HowMany);

		for (int32 Index = 0; Index < HowMany; ++Index)
		{
			Out.Where.Add(Component->GetBoneTransform(Index).GetLocation());
		}
	}

	/**
	 * How many world units fit in one pixel, at that depth.
	 *
	 * It holds for perspective and for orthographic: in orthographic W comes out
	 * as 1 and M[0][0] already carries the framing's width, so the same sum serves
	 * both cases.
	 */
	static float WorldPerPixel(const FSceneView& View, const FVector& Where)
	{
		const float W = static_cast<float>(View.WorldToScreen(Where).W);
		const float Focus = static_cast<float>(View.ViewMatrices.GetViewToClip().M[0][0]);
		const float Width = static_cast<float>(FMath::Max(View.UnscaledViewRect.Width(), 1));

		return FMath::Abs(W) * 2.f / FMath::Max(Focus * Width, UE_KINDA_SMALL_NUMBER);
	}

}

bool FFofuxoBonesOnScreen::IsOn()
{
	if (!FofuxoBones::bReadFromIni)
	{
		FofuxoBones::bReadFromIni = true;
		GConfig->GetBool(FofuxoBones::IniSection, FofuxoBones::IniKey,
			FofuxoBones::bOn, GEditorPerProjectIni);
	}

	return FofuxoBones::bOn;
}

void FFofuxoBonesOnScreen::Toggle()
{
	// The lazy read first: without this the next query after this one would go to
	// the ini and undo the choice.
	IsOn();

	FofuxoBones::bOn = !FofuxoBones::bOn;

	GConfig->SetBool(FofuxoBones::IniSection, FofuxoBones::IniKey,
		FofuxoBones::bOn, GEditorPerProjectIni);
}

void FFofuxoBonesOnScreen::Follow(FIKRetargetEditor& Editor)
{
	const TSharedRef<FIKRetargetEditorController> Who = Editor.GetController();

	UIKRetargeter* Asset = Who->AssetController != nullptr
		? Who->AssetController->GetAsset()
		: nullptr;

	if (Asset == nullptr)
	{
		return;
	}

	if (IsOn())
	{
		if (!FofuxoBones::Stored.Contains(Asset))
		{
			FofuxoBones::Stored.Add(Asset, Asset->BoneDrawSize);
		}

		// Reapplied on every walk, and not only on the switch: reopening the asset
		// or a Ctrl+Z bring the value back from disk, and the engine's drawing
		// would fatten again underneath the sticks.
		Asset->BoneDrawSize = FofuxoBones::ShrunkenSize;

		return;
	}

	if (const float* Before = FofuxoBones::Stored.Find(Asset))
	{
		Asset->BoneDrawSize = *Before;
		FofuxoBones::Stored.Remove(Asset);
	}
}

void FFofuxoBonesOnScreen::Forget()
{
	for (const TTuple<TWeakObjectPtr<UIKRetargeter>, float>& Pair : FofuxoBones::Stored)
	{
		if (UIKRetargeter* Asset = Pair.Key.Get())
		{
			Asset->BoneDrawSize = Pair.Value;
		}
	}

	FofuxoBones::Stored.Reset();
}

void FFofuxoBonesOnScreen::Draw(
	const FIKRetargetEditorController& Who,
	const FSceneView* View,
	FPrimitiveDrawInterface* PDI)
{
	if (!IsOn() || View == nullptr || PDI == nullptr)
	{
		return;
	}

	const UIKRetargeter* Asset = Who.AssetController != nullptr
		? Who.AssetController->GetAsset()
		: nullptr;

	if (Asset == nullptr)
	{
		return;
	}

	const UPersonaOptions* Options = GetDefault<UPersonaOptions>();
	const ERetargetSourceOrTarget Editable = Who.GetSourceOrTarget();
	const TArray<FName>& Selected = Who.GetSelectedBones();

	const FVector Right = View->GetViewRight();
	const FVector Up = View->GetViewUp();

	for (const ERetargetSourceOrTarget Side : {ERetargetSourceOrTarget::Source, ERetargetSourceOrTarget::Target})
	{
		// The same checkbox that hides the engine's skeleton hides the stick: they
		// are the same bone drawn two ways.
		const bool bInSight = Side == ERetargetSourceOrTarget::Source
			? Asset->bShowSourceSkeleton
			: Asset->bShowTargetSkeleton;

		if (!bInSight)
		{
			continue;
		}

		FofuxoBones::FCharacterToDraw Character;
		FofuxoBones::Gather(Who, Side, Character);

		if (!Character.Serves())
		{
			continue;
		}

		const bool bEditable = Side == Editable;

		// The side that isn't being edited comes out dimmed, as in the engine: it
		// is a reference, and there is nothing to click on it.
		const FLinearColor Normal = bEditable ? Options->DefaultBoneColor : Options->DisabledBoneColor;

		const FReferenceSkeleton& Skeleton = Character.Skeleton();

		for (int32 Index = 0; Index < Character.Where.Num(); ++Index)
		{
			const FVector& Where = Character.Where[Index];

			const bool bSelected = bEditable && Selected.Contains(Skeleton.GetBoneName(Index));
			const FLinearColor Colour = bSelected ? Options->SelectedBoneColor : Normal;

			// Constant size on screen: it is what sets this apart from the engine's
			// drawing, which is measured in world units and therefore vanishes as
			// the camera pulls back.
			const float PerPixel = FofuxoBones::WorldPerPixel(*View, Where);

			// SDPG_Foreground: the bone shows through the mesh. Without that the
			// hand hides the fingers, which is exactly where this drawing is of any
			// use.
			DrawCircle(
				PDI, Where, Right, Up, Colour,
				PerPixel * FofuxoBones::JointRadius,
				/*NumSides*/ 12,
				SDPG_Foreground,
				PerPixel * FofuxoBones::StickThickness);

			const int32 Parent = Skeleton.GetParentIndex(Index);
			if (Character.Where.IsValidIndex(Parent))
			{
				// The stick belongs to the parent, as in Blender and as in the
				// engine: the line leaves the bone's joint and reaches the child's,
				// and clicking it selects the parent.
				const FLinearColor LineColour =
					(bEditable && Selected.Contains(Skeleton.GetBoneName(Parent)))
						? Options->SelectedBoneColor
						: Normal;

				PDI->DrawLine(
					Character.Where[Parent], Where, LineColour, SDPG_Foreground,
					PerPixel * FofuxoBones::StickThickness);
			}
		}
	}
}

bool FFofuxoBonesOnScreen::IsBoneProxy(HHitProxy* Proxy)
{
	// By the type's name, and not by IsA: HIKRetargetEditorBoneProxy::StaticGetType
	// belongs to IKRigEditor and is not exported, so there is nothing to compare
	// against from outside. The name is in the HHitProxyType itself, put there by
	// IMPLEMENT_HIT_PROXY.
	return Proxy != nullptr
		&& Proxy->GetType() != nullptr
		&& FCString::Strcmp(Proxy->GetType()->GetName(), FofuxoBones::ProxyName) == 0;
}

HHitProxy* FFofuxoBonesOnScreen::BoneNearCursor(FViewport& Viewport, const int32 X, const int32 Y)
{
	const FIntPoint Size = Viewport.GetSizeXY();
	if (Size.X <= 0 || Size.Y <= 0)
	{
		return nullptr;
	}

	const FIntRect Box(
		FMath::Max(X - FofuxoBones::RadiusInPixels, 0),
		FMath::Max(Y - FofuxoBones::RadiusInPixels, 0),
		FMath::Min(X + FofuxoBones::RadiusInPixels + 1, Size.X),
		FMath::Min(Y + FofuxoBones::RadiusInPixels + 1, Size.Y));

	if (Box.Width() <= 0 || Box.Height() <= 0)
	{
		return nullptr;
	}

	TArray<HHitProxy*> Map;
	Viewport.GetHitProxyMap(Box, Map);

	if (Map.Num() < Box.Width() * Box.Height())
	{
		return nullptr;
	}

	HHitProxy* Best = nullptr;
	int32 SmallestDistance = MAX_int32;

	for (int32 Row = 0; Row < Box.Height(); ++Row)
	{
		for (int32 Column = 0; Column < Box.Width(); ++Column)
		{
			HHitProxy* Proxy = Map[Row * Box.Width() + Column];
			if (!IsBoneProxy(Proxy))
			{
				continue;
			}

			const int32 Sideways = Box.Min.X + Column - X;
			const int32 Downwards = Box.Min.Y + Row - Y;
			const int32 Distance = Sideways * Sideways + Downwards * Downwards;

			if (Distance < SmallestDistance)
			{
				SmallestDistance = Distance;
				Best = Proxy;
			}
		}
	}

	return Best;
}
