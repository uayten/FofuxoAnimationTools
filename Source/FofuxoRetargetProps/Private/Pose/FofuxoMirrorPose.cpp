// Fofuxo -- mirroring the retarget pose

#include "FofuxoMirrorPose.h"

#include "FofuxoLiveRetarget.h"

#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/ConfigCacheIni.h"
#include "ReferenceSkeleton.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"
#include "Toolkits/AssetEditorToolkit.h"

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoMirror, Log, All);

namespace FofuxoMirror
{
	static const TCHAR* IniSection = TEXT("FofuxoRetargetProps");
	static const TCHAR* IniKey = TEXT("MirrorPose");

	/** How far apart two offsets may be and still count as the same. */
	static constexpr float Slack = 1.0e-6f;

	// -------------------------------------------------------------------------
	// Names
	// -------------------------------------------------------------------------

	/** How the side appears written. The first is left, the second right. */
	struct FPair
	{
		const TCHAR* Left;
		const TCHAR* Right;
	};

	static const FPair Pairs[] =
	{
		{ TEXT("l"),    TEXT("r")     },
		{ TEXT("left"), TEXT("right") },
		{ TEXT("lt"),   TEXT("rt")    },
	};

	static bool IsSeparator(const TCHAR C)
	{
		// The colon is Mixamo's, which exports "mixamorig:LeftArm".
		return C == TEXT('_') || C == TEXT('.') || C == TEXT('-') || C == TEXT(' ') || C == TEXT(':');
	}

	/**
	 * The new word with the old one's case: "L" becomes "R", "l" becomes "r",
	 * "Left" becomes "Right", "LEFT" becomes "RIGHT".
	 */
	static FString WithTheCaseOf(const FString& Model, const FString& New)
	{
		bool bHasLower = false;
		bool bHasUpper = false;

		for (const TCHAR C : Model)
		{
			bHasLower |= FChar::IsLower(C);
			bHasUpper |= FChar::IsUpper(C);
		}

		if (bHasUpper && !bHasLower)
		{
			return New.ToUpper();
		}

		if (bHasLower && !bHasUpper)
		{
			return New.ToLower();
		}

		// Mixed, which in practice is "Left": first letter upper, rest lower.
		FString Result = New.ToLower();
		if (Result.Len() > 0)
		{
			Result[0] = FChar::ToUpper(Result[0]);
		}

		return Result;
	}

	/** The other side of a segment that is only the side ("l", "Left"), or empty. */
	static FString SwapSegment(const FString& Segment)
	{
		for (const FPair& Pair : Pairs)
		{
			if (Segment.Equals(Pair.Left, ESearchCase::IgnoreCase))
			{
				return WithTheCaseOf(Segment, Pair.Right);
			}

			if (Segment.Equals(Pair.Right, ESearchCase::IgnoreCase))
			{
				return WithTheCaseOf(Segment, Pair.Left);
			}
		}

		return FString();
	}

	/**
	 * The other side of a segment with the side glued on -- "HandL", "LHand",
	 * "LeftShoulder", "ShoulderLeft".
	 *
	 * With no separator, the only thing marking where the side starts is the
	 * case: the side's piece has to open with a capital, and its neighbour has to
	 * be lowercase or a digit. Without that, "Control" ended up as "Contror" and
	 * "Barrel" became the partner of "Barrer".
	 */
	static FString SwapGlued(const FString& Segment)
	{
		const int32 Length = Segment.Len();
		if (Length < 2)
		{
			return FString();
		}

		// Prefix: LHand, LeftShoulder. What comes after the side has to be a
		// capital, or "lowerarm" would start with left's "l".
		for (const FPair& Pair : Pairs)
		{
			const TCHAR* Sides[] = { Pair.Left, Pair.Right };
			const TCHAR* Others[] = { Pair.Right, Pair.Left };

			for (int32 Which = 0; Which < 2; ++Which)
			{
				const int32 HowMuch = FCString::Strlen(Sides[Which]);

				if (Length <= HowMuch || !FChar::IsUpper(Segment[0]))
				{
					continue;
				}

				if (Segment.Left(HowMuch).Equals(Sides[Which], ESearchCase::IgnoreCase)
					&& FChar::IsUpper(Segment[HowMuch]))
				{
					return WithTheCaseOf(Segment.Left(HowMuch), Others[Which]) + Segment.Mid(HowMuch);
				}
			}
		}

		// Suffix: HandL, Spine01L, ShoulderLeft.
		for (const FPair& Pair : Pairs)
		{
			const TCHAR* Sides[] = { Pair.Left, Pair.Right };
			const TCHAR* Others[] = { Pair.Right, Pair.Left };

			for (int32 Which = 0; Which < 2; ++Which)
			{
				const int32 HowMuch = FCString::Strlen(Sides[Which]);
				const int32 Where = Length - HowMuch;

				if (Where < 1 || !FChar::IsUpper(Segment[Where]))
				{
					continue;
				}

				const TCHAR Neighbour = Segment[Where - 1];
				if (!FChar::IsLower(Neighbour) && !FChar::IsDigit(Neighbour))
				{
					continue;
				}

				if (Segment.Mid(Where).Equals(Sides[Which], ESearchCase::IgnoreCase))
				{
					return Segment.Left(Where) + WithTheCaseOf(Segment.Mid(Where), Others[Which]);
				}
			}
		}

		return FString();
	}

	/**
	 * A bone's partner: the only candidate that exists in this skeleton.
	 *
	 * Two valid candidates is real ambiguity -- "L_arm_l" could have "R_arm_l" or
	 * "L_arm_r" as its partner -- and guessing would be worse than not mirroring.
	 */
	static FName Partner(const FName Bone, const FReferenceSkeleton& Skeleton)
	{
		TArray<FString> Candidates;
		FFofuxoMirrorPose::MirroredNames(Bone.ToString(), Candidates);

		TArray<FName> Valid;
		for (const FString& Candidate : Candidates)
		{
			const FName Name(*Candidate);

			if (Name != Bone && Skeleton.FindBoneIndex(Name) != INDEX_NONE)
			{
				Valid.AddUnique(Name);
			}
		}

		if (Valid.Num() == 1)
		{
			return Valid[0];
		}

		if (Valid.Num() > 1)
		{
			UE_LOG(LogFofuxoMirror, Verbose,
				TEXT("\"%s\" has more than one possible bone on the other side -- it gets no mirror."),
				*Bone.ToString());
		}

		return NAME_None;
	}

	// -------------------------------------------------------------------------
	// Geometry
	// -------------------------------------------------------------------------

	/**
	 * The same rotation seen in the mirror, with the plane through the origin and
	 * its normal on the given axis.
	 *
	 * Reflecting a rotation swaps handedness, so what holds is not reflecting but
	 * conjugating by the reflection: M R M. In quaternion terms that means keeping
	 * the normal axis's component and flipping the sign of the other two.
	 */
	static FQuat Mirror(const FQuat& Rotation, const int32 Axis)
	{
		return FQuat(
			Axis == 0 ? Rotation.X : -Rotation.X,
			Axis == 1 ? Rotation.Y : -Rotation.Y,
			Axis == 2 ? Rotation.Z : -Rotation.Z,
			Rotation.W);
	}
}

void FFofuxoMirrorPose::MirroredNames(const FString& Name, TArray<FString>& OutCandidates)
{
	// The pieces between separators, kept by position so the name can be put back
	// together with the same separators it had.
	TArray<TPair<int32, int32>> Segments;

	int32 Start = 0;
	for (int32 Index = 0; Index <= Name.Len(); ++Index)
	{
		if (Index == Name.Len() || FofuxoMirror::IsSeparator(Name[Index]))
		{
			if (Index > Start)
			{
				Segments.Emplace(Start, Index - Start);
			}

			Start = Index + 1;
		}
	}

	for (const TPair<int32, int32>& Segment : Segments)
	{
		const FString Text = Name.Mid(Segment.Key, Segment.Value);

		FString Swapped = FofuxoMirror::SwapSegment(Text);
		if (Swapped.IsEmpty())
		{
			Swapped = FofuxoMirror::SwapGlued(Text);
		}

		if (!Swapped.IsEmpty())
		{
			OutCandidates.Add(Name.Left(Segment.Key) + Swapped + Name.Mid(Segment.Key + Segment.Value));
		}
	}
}

void FFofuxoMirrorPose::Start()
{
	GConfig->GetBool(FofuxoMirror::IniSection, FofuxoMirror::IniKey, bOn, GEditorPerProjectIni);

	// Every frame, and not every half second like the attachments' one: here the
	// user has the gizmo in hand and the other side has to keep up.
	Ticker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FFofuxoMirrorPose::Tick), 0.0f);
}

void FFofuxoMirrorPose::Stop()
{
	if (Ticker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
		Ticker.Reset();
	}

	Watched.Reset();
}

void FFofuxoMirrorPose::Toggle()
{
	bOn = !bOn;
	GConfig->SetBool(FofuxoMirror::IniSection, FofuxoMirror::IniKey, bOn, GEditorPerProjectIni);
}

void FFofuxoMirrorPose::Follow(FIKRetargetEditor& Editor)
{
	const TSharedRef<FAssetEditorToolkit> Toolkit = StaticCastSharedRef<FAssetEditorToolkit>(Editor.AsShared());

	for (const FWatched& One : Watched)
	{
		if (One.Toolkit.Pin() == Toolkit)
		{
			return;
		}
	}

	FWatched New;
	New.Toolkit = Toolkit;
	Watched.Add(MoveTemp(New));
}

bool FFofuxoMirrorPose::Tick(float)
{
	Watched.RemoveAll([](const FWatched& One) { return !One.Toolkit.IsValid(); });

	for (FWatched& One : Watched)
	{
		const TSharedPtr<FAssetEditorToolkit> Toolkit = One.Toolkit.Pin();
		if (!Toolkit.IsValid() || Toolkit->GetEditorName() != FName("IKRetargetEditor"))
		{
			continue;
		}

		Check(One, *static_cast<FIKRetargetEditor*>(Toolkit.Get()));
	}

	return true;
}

void FFofuxoMirrorPose::Check(FWatched& One, FIKRetargetEditor& Editor)
{
	const TSharedRef<FIKRetargetEditorController> Controller = Editor.GetController();

	UIKRetargeterController* AssetController = Controller->AssetController;
	if (AssetController == nullptr)
	{
		return;
	}

	const ERetargetSourceOrTarget Side = Controller->GetSourceOrTarget();
	USkeletalMesh* Mesh = AssetController->GetPreviewMesh(Side);
	const FName PoseName = AssetController->GetCurrentRetargetPoseName(Side);

	// Through Find, and not GetCurrentRetargetPose: that one indexes the map
	// directly and blows up if the name isn't there, which happens in the middle
	// of a Delete.
	FIKRetargetPose* Pose = AssetController->GetRetargetPoses(Side).Find(PoseName);

	if (Mesh == nullptr || Pose == nullptr)
	{
		One.Snapshot.Reset();
		One.bHasCache = false;
		return;
	}

	if (!One.bHasCache || One.Mesh != Mesh || One.Side != static_cast<uint8>(Side))
	{
		RebuildCache(One, Mesh);
	}

	const TMap<FName, FQuat>& Current = Pose->GetAllDeltaRotations();

	// Switching character, pose or mode is not an edit -- all that is worth doing
	// is syncing the copy, or the next rotation would be compared against someone
	// else's pose.
	const bool bSwitched =
		One.Mesh != Mesh ||
		One.Side != static_cast<uint8>(Side) ||
		One.Pose != PoseName;

	// It applies in both places the retarget pose is edited. The watcher compares
	// offset maps and neither knows nor wants to know who wrote them -- and that
	// is why Live Retarget's gizmo and Alt+R come in through the same door as the
	// engine's gizmo.
	const ERetargeterOutputMode Mode = Controller->GetRetargeterMode();

	const bool bEditing = Mode == ERetargeterOutputMode::EditRetargetPose
		|| (FFofuxoLiveRetarget::IsOn() && Mode == ERetargeterOutputMode::RunRetarget);

	if (bSwitched || !bOn || !bEditing)
	{
		One.Mesh = Mesh;
		One.Side = static_cast<uint8>(Side);
		One.Pose = PoseName;
		One.Snapshot = Current;
		return;
	}

	// What changed since last frame. A bone that vanished from the map went back
	// to identity -- that is a Reset, and it counts as a change too.
	TSet<FName> Changed;

	for (const TTuple<FName, FQuat>& Pair : Current)
	{
		const FQuat* Before = One.Snapshot.Find(Pair.Key);
		const FQuat Old = Before != nullptr ? *Before : FQuat::Identity;

		if (!Old.Equals(Pair.Value, FofuxoMirror::Slack))
		{
			Changed.Add(Pair.Key);
		}
	}

	for (const TTuple<FName, FQuat>& Pair : One.Snapshot)
	{
		if (!Current.Contains(Pair.Key) && !Pair.Value.Equals(FQuat::Identity, FofuxoMirror::Slack))
		{
			Changed.Add(Pair.Key);
		}
	}

	if (Changed.IsEmpty())
	{
		return;
	}

	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();

	TArray<TTuple<FName, FQuat>> ToWrite;

	for (const FName& Bone : Changed)
	{
		const FName OtherSide = One.Partners.FindRef(Bone);
		if (OtherSide.IsNone() || OtherSide == Bone)
		{
			continue;
		}

		// Both sides changed on the same frame: it was on purpose -- the gizmo
		// with both selected, a global Auto Align, a Ctrl+Z. Mirroring here would
		// be one of the two changes eating the other, and which one depends on the
		// order the TSet handed them back in.
		if (Changed.Contains(OtherSide))
		{
			continue;
		}

		const int32 BoneIndex = Skeleton.FindBoneIndex(Bone);
		const int32 OtherIndex = Skeleton.FindBoneIndex(OtherSide);

		if (!One.RefComponent.IsValidIndex(BoneIndex) || !One.RefComponent.IsValidIndex(OtherIndex))
		{
			continue;
		}

		const FQuat* Found = Current.Find(Bone);
		const FQuat Delta = Found != nullptr ? *Found : FQuat::Identity;

		// The offset is in bone space, and the two sides almost never have their
		// axes pointing the same way -- flipping the sign of two components as it
		// stands would give rubbish. So: carry the delta into component space
		// through this bone's ref pose, mirror it there, and bring it back through
		// the partner's ref pose.
		const FQuat& RefBone = One.RefComponent[BoneIndex];
		const FQuat& RefOther = One.RefComponent[OtherIndex];

		const FQuat InComponent = RefBone * Delta * RefBone.Inverse();
		const FQuat Mirrored = FofuxoMirror::Mirror(InComponent, One.Axis);
		const FQuat InOther = (RefOther.Inverse() * Mirrored * RefOther).GetNormalized();

		const FQuat* Had = Current.Find(OtherSide);
		if (Had != nullptr && Had->Equals(InOther, FofuxoMirror::Slack))
		{
			continue;
		}

		ToWrite.Emplace(OtherSide, InOther);
	}

	if (!ToWrite.IsEmpty())
	{
		// During a gizmo drag the transaction is already open and the asset has
		// already been marked -- this Modify joins it, and Ctrl+Z undoes both
		// sides together. Outside a drag it just dirties the package, which is
		// what we want.
		if (UIKRetargeter* Asset = AssetController->GetAsset())
		{
			Asset->Modify();
		}

		for (const TTuple<FName, FQuat>& Pair : ToWrite)
		{
			AssetController->SetRotationOffsetForRetargetPoseBone(Pair.Key, Pair.Value, Side);
		}
	}

	// After the writes, so the next frame doesn't read the mirror as an edit.
	One.Snapshot = Pose->GetAllDeltaRotations();
}

void FFofuxoMirrorPose::RebuildCache(FWatched& One, USkeletalMesh* Mesh)
{
	One.RefComponent.Reset();
	One.Partners.Reset();
	One.Axis = 1;
	One.bHasCache = true;

	if (Mesh == nullptr)
	{
		return;
	}

	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
	const TArray<FTransform>& Local = Skeleton.GetRefBonePose();
	const int32 HowMany = Skeleton.GetNum();

	// The ref pose in component space. The bone list comes with the parent always
	// before the child, so one pass is enough.
	TArray<FTransform> Component;
	Component.SetNum(HowMany);

	for (int32 Index = 0; Index < HowMany; ++Index)
	{
		const int32 Parent = Skeleton.GetParentIndex(Index);
		Component[Index] = Parent == INDEX_NONE ? Local[Index] : Local[Index] * Component[Parent];
	}

	One.RefComponent.SetNum(HowMany);
	for (int32 Index = 0; Index < HowMany; ++Index)
	{
		One.RefComponent[Index] = Component[Index].GetRotation().GetNormalized();
	}

	// Which axis the pairs are separated on. Summed over every pair, the right
	// axis wins by a mile -- it cannot be pinned to Y because a mesh that came
	// from Blender or Maya may have the character facing another way.
	FVector Separation = FVector::ZeroVector;

	for (int32 Index = 0; Index < HowMany; ++Index)
	{
		const FName Bone = Skeleton.GetBoneName(Index);
		const FName OtherSide = FofuxoMirror::Partner(Bone, Skeleton);

		One.Partners.Add(Bone, OtherSide);

		if (OtherSide.IsNone())
		{
			continue;
		}

		// Only half of each pair, or every one would go in twice.
		const int32 OtherIndex = Skeleton.FindBoneIndex(OtherSide);
		if (OtherIndex <= Index)
		{
			continue;
		}

		const FVector Difference = Component[Index].GetLocation() - Component[OtherIndex].GetLocation();
		Separation += FVector(FMath::Abs(Difference.X), FMath::Abs(Difference.Y), FMath::Abs(Difference.Z));
	}

	// A tie, which is also the case of there being no pair at all, stays on Y.
	if (Separation.X > Separation.Y && Separation.X > Separation.Z)
	{
		One.Axis = 0;
	}
	else if (Separation.Z > Separation.X && Separation.Z > Separation.Y)
	{
		One.Axis = 2;
	}

	int32 WithPartner = 0;
	for (const TTuple<FName, FName>& Pair : One.Partners)
	{
		WithPartner += Pair.Value.IsNone() ? 0 : 1;
	}

	static const TCHAR* AxisName[] = { TEXT("X"), TEXT("Y"), TEXT("Z") };

	UE_LOG(LogFofuxoMirror, Display,
		TEXT("Mirror of %s: %d bones out of %d have a partner, mirror plane with its normal on %s."),
		*Mesh->GetName(), WithPartner, HowMany, AxisName[One.Axis]);
}
