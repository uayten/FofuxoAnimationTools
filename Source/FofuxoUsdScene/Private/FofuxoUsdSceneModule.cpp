// Fofuxo -- USD scene
//
// Writes several animations into a single USD stage: one SkelRoot, one Skeleton,
// and one SkelAnimation prim per animation.
//
// Why this is a separate module: the engine's conversions take Pixar types
// (pxr::UsdPrim), which forces a link against the SDK. Leaving that in the main
// module would make the whole plugin require USD to be enabled. Here, if USD is
// not present, this module simply doesn't hand over the delegate and the rest of
// the plugin carries on exporting FBX.

#include "FofuxoSceneWriters.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Modules/ModuleManager.h"
#include "ReferenceSkeleton.h"

#if USE_USD_SDK
#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDLayerUtils.h"
#include "USDObjectUtils.h"
#include "USDSkeletalDataConversion.h"
#include "USDStageOptions.h"
#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#include "USDMemory.h"

#include "USDIncludesStart.h"
#include "pxr/usd/usdSkel/animation.h"
#include "pxr/usd/usdSkel/skeleton.h"
#include "USDIncludesEnd.h"
#endif	  // USE_USD_SDK

#define LOCTEXT_NAMESPACE "FofuxoUsdScene"

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoUsdScene, Log, All);

#if USE_USD_SDK
namespace FofuxoUsd
{
	/**
	 * A valid USD prim name: letters, digits and underscore, not starting with a
	 * digit. Unreal's asset names almost always pass already; almost is not
	 * always, and an invalid name makes DefinePrim come back empty.
	 */
	static FString PrimName(const FString& Name)
	{
		FString Clean;
		Clean.Reserve(Name.Len() + 1);

		for (const TCHAR Letter : Name)
		{
			Clean.AppendChar(FChar::IsAlnum(Letter) || Letter == TEXT('_') ? Letter : TEXT('_'));
		}

		if (Clean.IsEmpty() || FChar::IsDigit(Clean[0]))
		{
			Clean.InsertAt(0, TEXT('_'));
		}

		return Clean;
	}

	/**
	 * A name USD accepts as an identifier: it starts with a letter or an
	 * underscore, and after that only letters, digits or underscores.
	 *
	 * The engine's cleanup (SanitizeObjectName) is not enough. It swaps the set
	 * of characters that are invalid for an Unreal object name -- which includes
	 * dot and slash, but lets through hyphens, accents and names starting with a
	 * digit. A bone called "arm-l", or an accented one, goes through that
	 * function and becomes a joint path USD considers invalid, and then the
	 * reader rejects the whole file.
	 */
	static FString UsdIdentifier(const FString& Name)
	{
		FString Out;
		Out.Reserve(Name.Len() + 1);

		for (const TCHAR Letter : Name)
		{
			const bool bAccepted = (Letter >= TEXT('a') && Letter <= TEXT('z'))
				|| (Letter >= TEXT('A') && Letter <= TEXT('Z'))
				|| (Letter >= TEXT('0') && Letter <= TEXT('9'))
				|| Letter == TEXT('_');

			Out.AppendChar(bAccepted ? Letter : TEXT('_'));
		}

		if (Out.IsEmpty() || (Out[0] >= TEXT('0') && Out[0] <= TEXT('9')))
		{
			Out.InsertAt(0, TEXT('_'));
		}

		return Out;
	}

	/**
	 * The joint paths, in bone order, already valid and unique.
	 *
	 * They go to the Skeleton prim and to each animation's prim, identical: both
	 * arrays are indexed by the same bone order, so swapping both for the same
	 * list keeps everything aligned with the transform matrices.
	 */
	static TArray<FString> JointPaths(const FReferenceSkeleton& Reference, TArray<FString>& OutRenamed)
	{
		const TArray<FMeshBoneInfo>& Bones = Reference.GetRefBoneInfo();

		TArray<FString> Paths;
		Paths.SetNum(Bones.Num());

		TSet<FString> AlreadyUsed;

		for (int32 Index = 0; Index < Bones.Num(); ++Index)
		{
			const FString Original = Bones[Index].Name.ToString();
			FString Name = UsdIdentifier(Original);

			// Two siblings landing on the same name after the cleanup have to be
			// pulled apart, or they become the same path.
			const int32 Parent = Bones[Index].ParentIndex;
			const FString Prefix = Paths.IsValidIndex(Parent) && Parent >= 0 ? Paths[Parent] + TEXT("/") : FString();

			FString Attempt = Prefix + Name;
			int32 Suffix = 2;
			while (AlreadyUsed.Contains(Attempt))
			{
				Attempt = Prefix + Name + FString::Printf(TEXT("_%d"), Suffix++);
			}

			AlreadyUsed.Add(Attempt);
			Paths[Index] = Attempt;

			const FString Leaf = Attempt.RightChop(Prefix.Len());
			if (Leaf != Original)
			{
				OutRenamed.Add(FString::Printf(TEXT("%s -> %s"), *Original, *Leaf));
			}
		}

		return Paths;
	}

	/** Puts the joint list on the attribute, over what the engine wrote. */
	static void WriteJoints(const TArray<FString>& Paths, pxr::UsdAttribute Attribute)
	{
		if (!Attribute)
		{
			return;
		}

		FScopedUsdAllocs Allocs;

		pxr::VtArray<pxr::TfToken> Joints;
		Joints.reserve(Paths.Num());

		for (const FString& Path : Paths)
		{
			Joints.push_back(pxr::TfToken(TCHAR_TO_UTF8(*Path)));
		}

		Attribute.Set(Joints);
	}

	/**
	 * The mesh the engine's conversion will take the animation's joints from.
	 *
	 * It is not the mesh chosen in the window: ConvertAnimSequence looks up the
	 * preview mesh of the animation's skeleton by itself and, failing that, any
	 * compatible mesh. Writing the Skeleton prim from another mesh would leave
	 * the file with two sets of joints that don't talk to each other.
	 */
	static USkeletalMesh* MeshTheEngineWillUse(UAnimSequence* Sequence)
	{
		USkeleton* ItsSkeleton = Sequence != nullptr ? Sequence->GetSkeleton() : nullptr;
		if (ItsSkeleton == nullptr)
		{
			return nullptr;
		}

		if (USkeletalMesh* FromPreview = ItsSkeleton->GetAssetPreviewMesh(Sequence))
		{
			return FromPreview;
		}

		return ItsSkeleton->FindCompatibleMesh();
	}

	static bool Write(const FFofuxoSceneRequest& Request, FText& OutError)
	{
		// An empty list gives a stage with just the SkelRoot, the skeleton and
		// the mesh -- no SkelAnimation prim, and no time range.
		if (Request.Mesh == nullptr)
		{
			OutError = LOCTEXT("SceneNoMesh", "Without a Skeletal Mesh there is no writing the scene's skeleton.");
			return false;
		}

		UE::FUsdStage Stage = UnrealUSDWrapper::NewStage(*Request.Path);
		if (!Stage)
		{
			OutError = FText::Format(
				LOCTEXT("SceneNoStage", "I could not create the USD stage at {0}."),
				FText::FromString(Request.Path));
			return false;
		}

		UsdUtils::SetUsdStageMetersPerUnit(Stage, Request.MetresPerUnit);
		UsdUtils::SetUsdStageUpAxis(Stage, Request.bYUp ? EUsdUpAxis::YAxis : EUsdUpAxis::ZAxis);

		// The stage's rate applies to everyone: ConvertAnimSequence bakes the
		// keys at that resolution. Hence the highest rate of the batch, and not
		// the first animation's -- with the first one's, a 60 fps animation in a
		// file that opened with a 30 fps one would lose half its keys.
		double FramesPerSecond = 0.0;
		double LongestDuration = 0.0;

		for (const UAnimSequence* Sequence : Request.Animations)
		{
			if (Sequence == nullptr)
			{
				continue;
			}

			FramesPerSecond = FMath::Max(FramesPerSecond, Sequence->GetSamplingFrameRate().AsDecimal());
			LongestDuration = FMath::Max(LongestDuration, static_cast<double>(Sequence->GetPlayLength()));
		}

		if (FramesPerSecond <= 0.0)
		{
			FramesPerSecond = 30.0;
		}

		Stage.SetTimeCodesPerSecond(FramesPerSecond);

		const UE::FSdfPath RootPath(TEXT("/Root"));

		UE::FUsdPrim Root = Stage.DefinePrim(RootPath, TEXT("SkelRoot"));
		if (!Root)
		{
			OutError = LOCTEXT("SceneNoRoot", "I could not create the scene's SkelRoot.");
			return false;
		}

		Stage.SetDefaultPrim(Root);

		// The file's skeleton has to be the one the animations will cite. With
		// no animation at all, the mesh chosen in the window is what rules.
		USkeletalMesh* SkeletonMesh = Request.Animations.Num() > 0
			? MeshTheEngineWillUse(Request.Animations[0])
			: nullptr;

		if (SkeletonMesh == nullptr)
		{
			SkeletonMesh = Request.Mesh;
		}

		// Two animations resolving to different meshes don't fit in the same
		// file: there is only one Skeleton prim, and one of the two would end up
		// citing joints that don't exist in it.
		for (UAnimSequence* Sequence : Request.Animations)
		{
			USkeletalMesh* Theirs = MeshTheEngineWillUse(Sequence);
			if (Theirs != nullptr && Theirs != SkeletonMesh)
			{
				OutError = FText::Format(
					LOCTEXT("SceneTwoMeshes",
						"{0} uses the mesh {1}, but the file is being written with {2}'s skeleton. "
						"Export those groups separately, or fix the skeleton's Preview Mesh."),
					FText::FromString(Sequence->GetName()),
					FText::FromString(Theirs->GetName()),
					FText::FromString(SkeletonMesh->GetName()));
				return false;
			}
		}

		const FReferenceSkeleton& Reference = SkeletonMesh->GetRefSkeleton();
		if (Reference.GetRefBoneInfo().Num() == 0)
		{
			OutError = FText::Format(
				LOCTEXT("SceneNoBones", "{0}'s skeleton has no bones at all."),
				FText::FromString(SkeletonMesh->GetName()));
			return false;
		}

		TArray<FString> Renamed;
		const TArray<FString> Joints = JointPaths(Reference, Renamed);

		if (Renamed.Num() > 0)
		{
			UE_LOG(LogFofuxoUsdScene, Display,
				TEXT("%d bone(s) had their name adjusted to fit USD. The file uses the adjusted names:"),
				Renamed.Num());

			for (const FString& Swap : Renamed)
			{
				UE_LOG(LogFofuxoUsdScene, Display, TEXT("    %s"), *Swap);
			}
		}

		// The mesh comes in through the engine's conversion, which already
		// creates the skeleton, the Mesh prims and the blend shapes inside the
		// SkelRoot, all tied together. Writing the skeleton on the side would
		// give two prims -- theirs is called "Skel", not "Skeleton" -- and the
		// mesh would be bound to theirs and the animations to mine.
		//
		// LOD 0 only: the others multiply the file without serving animation.
		{
			pxr::UsdPrim RawRoot{Root};
			if (!UnrealToUsd::ConvertSkeletalMesh(SkeletonMesh, RawRoot, pxr::UsdTimeCode::Default(), nullptr, 0, 0))
			{
				OutError = FText::Format(
					LOCTEXT("SceneMeshFailed", "The engine did not convert the mesh {0}."),
					FText::FromString(SkeletonMesh->GetName()));
				return false;
			}
		}

		UE::FUsdPrim Skeleton = Stage.GetPrimAtPath(
			RootPath.AppendChild(UnrealIdentifiers::ExportedSkeletonPrimName));

		if (!Skeleton)
		{
			OutError = LOCTEXT("SceneNoSkeleton", "The mesh conversion left no skeleton prim in the scene.");
			return false;
		}

		{
			// Over what the engine wrote: its cleanup lets through names USD
			// does not accept.
			pxr::UsdSkelSkeleton UsdSkeleton{pxr::UsdPrim(Skeleton)};
			WriteJoints(Joints, UsdSkeleton.CreateJointsAttr());
		}

		for (int32 Index = 0; Index < Request.Animations.Num(); ++Index)
		{
			UAnimSequence* Sequence = Request.Animations[Index];
			if (Sequence == nullptr)
			{
				continue;
			}

			const FString Name = PrimName(Sequence->GetName());

			UE::FUsdPrim AnimationPrim = Stage.DefinePrim(RootPath.AppendChild(*Name), TEXT("SkelAnimation"));
			if (!AnimationPrim)
			{
				OutError = FText::Format(
					LOCTEXT("SceneNoPrim", "I could not create the prim for the animation {0}."),
					FText::FromString(Sequence->GetName()));
				return false;
			}

			if (!UnrealToUsd::ConvertAnimSequence(Sequence, AnimationPrim))
			{
				OutError = FText::Format(
					LOCTEXT("SceneAnimationFailed", "The engine did not convert the animation {0}."),
					FText::FromString(Sequence->GetName()));
				return false;
			}

			// The same list as the skeleton's, for the same reason -- and it is
			// what keeps the two prims talking about the same bones.
			{
				pxr::UsdSkelAnimation UsdAnimation{pxr::UsdPrim(AnimationPrim)};
				WriteJoints(Joints, UsdAnimation.CreateJointsAttr());
			}

			// A stage has a single timeline, and a skeleton plays one animation
			// at a time. The others sit in the file waiting for the reader to
			// swap skel:animationSource -- which is how USD does a clip library.
			// The first one comes bound so the file opens showing something.
			if (Index == 0)
			{
				UsdUtils::BindAnimationSource(Skeleton, AnimationPrim);
			}
		}

		// The end of the range in the stage's timecodes: duration in seconds
		// times the rate. Counting the animation's frames would go wrong the
		// moment two different rates shared a file.
		//
		// Mesh only has no timeline: writing a 0-0 range would make the reader
		// show a frame of an animation that isn't there.
		if (Request.Animations.Num() > 0)
		{
			UsdUtils::AddTimeCodeRangeToLayer(
				Stage.GetRootLayer(),
				0.0,
				FMath::CeilToDouble(LongestDuration * FramesPerSecond));
		}

		if (!Stage.GetRootLayer().Save())
		{
			OutError = FText::Format(
				LOCTEXT("SceneNotSaved", "The file {0} was not written."),
				FText::FromString(Request.Path));
			return false;
		}

		return true;
	}
}
#endif	  // USE_USD_SDK

class FFofuxoUsdSceneModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
#if USE_USD_SDK
		FofuxoUsdSceneWriter().BindStatic(&FofuxoUsd::Write);
#endif
	}

	virtual void ShutdownModule() override
	{
		// Mandatory: the delegate lives in the other module and would point at
		// code from this DLL after it goes away.
		FofuxoUsdSceneWriter().Unbind();
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFofuxoUsdSceneModule, FofuxoUsdScene)
