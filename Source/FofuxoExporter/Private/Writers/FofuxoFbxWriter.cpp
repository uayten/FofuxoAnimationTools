// Fofuxo

#include "FofuxoFbxWriter.h"
#include "FofuxoName.h"

#include "FofuxoExportBatch.h"
#include "FofuxoExportOptions.h"
#include "FofuxoExportRequest.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Exporters/FbxExportOption.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopedSlowTask.h"
#include "ReferenceSkeleton.h"
#include "UObject/StrongObjectPtr.h"

// Private to UnrealEd. See the comment in Build.cs.
#include "FbxExporter.h"

#define LOCTEXT_NAMESPACE "FofuxoExporter"

namespace FofuxoPrivate
{
	/**
	 * The method that writes a take's curves already exists in the engine, takes
	 * an explicit FbxAnimLayer and does exactly what we need -- it is just
	 * private. This is the standard explicit-instantiation trick: the compiler
	 * does not check access when instantiating a template, so we can keep the
	 * pointer to the member and call through it.
	 *
	 * The alternative would be reimplementing bone curve sampling, which is
	 * where the tedious details live (axis conversion, root motion, custom
	 * attributes). Reusing it guarantees take 2 comes out just like take 1.
	 */
	template <typename Tag, typename Tag::Type Member>
	struct TThief
	{
		friend typename Tag::Type Steal(Tag)
		{
			return Member;
		}
	};

	struct FExportTake
	{
		using Type = void (UnFbx::FFbxExporter::*)(
			const UAnimSequence*, const USkeletalMesh*, TArray<FbxNode*>&,
			FbxAnimLayer*, FFrameTime, FFrameTime, float, float);

		friend Type Steal(FExportTake);
	};
	template struct TThief<FExportTake, &UnFbx::FFbxExporter::ExportAnimSequenceToFbx>;

	struct FFixInterpolation
	{
		using Type = void (UnFbx::FFbxExporter::*)(TArray<FbxNode*>&, FbxAnimLayer*);

		friend Type Steal(FFixInterpolation);
	};
	template struct TThief<FFixInterpolation, &UnFbx::FFbxExporter::CorrectAnimTrackInterpolation>;

	/**
	 * The open document's scene. The same trick, now on a data member.
	 *
	 * It exists because of the mesh-only export: the engine's ExportSkeletalMesh
	 * is public but hands back no node, and without the scene there is no
	 * converting axis and unit. On the path with animation the scene still comes
	 * from the bone node.
	 */
	struct FScene
	{
		using Type = FbxScene* UnFbx::FFbxExporter::*;

		friend Type Steal(FScene);
	};
	template struct TThief<FScene, &UnFbx::FFbxExporter::Scene>;

	// A friend function defined inside a class is only found by ADL. Redeclaring
	// it at namespace scope makes the qualified call work too.
	FExportTake::Type Steal(FExportTake);
	FFixInterpolation::Type Steal(FFixInterpolation);
	FScene::Type Steal(FScene);

	/**
	 * Rebuilds the array of bone nodes in the same order the engine's
	 * CreateSkeleton used -- FReferenceSkeleton index -- walking down the
	 * hierarchy from the root ExportAnimSequence handed back.
	 *
	 * Searching the whole scene by name would be shorter, but it would match any
	 * namesake; walking down from the right parent carries no such risk.
	 */
	static bool CollectBones(const USkeletalMesh* Mesh, FbxNode* BoneRoot, TArray<FbxNode*>& Out, FString& OutMissingBone)
	{
		const FReferenceSkeleton& Reference = Mesh->GetRefSkeleton();
		const int32 NumBones = Reference.GetRawBoneNum();

		Out.Reset();
		if (NumBones == 0 || BoneRoot == nullptr)
		{
			return false;
		}

		Out.Add(BoneRoot);

		for (int32 Index = 1; Index < NumBones; ++Index)
		{
			const FMeshBoneInfo& Bone = Reference.GetRefBoneInfo()[Index];

#if WITH_EDITORONLY_DATA
			const FbxString ExpectedName = UnFbx::FFbxDataConverter::ConvertToFbxString(Bone.ExportName);
#else
			const FbxString ExpectedName = UnFbx::FFbxDataConverter::ConvertToFbxString(Bone.Name);
#endif

			FbxNode* Parent = Out.IsValidIndex(Bone.ParentIndex) ? Out[Bone.ParentIndex] : nullptr;
			FbxNode* Found = nullptr;

			if (Parent != nullptr)
			{
				for (int32 Child = 0; Child < Parent->GetChildCount(); ++Child)
				{
					FbxNode* Node = Parent->GetChild(Child);
					if (FbxString(Node->GetName()) == ExpectedName)
					{
						Found = Node;
						break;
					}
				}
			}

			if (Found == nullptr)
			{
				OutMissingBone = Bone.Name.ToString();
				return false;
			}

			Out.Add(Found);
		}

		return true;
	}

	/** A take's time span, in seconds. */
	static FbxTimeSpan SpanOf(const UAnimSequence* Sequence)
	{
		const int32 NumFrames = Sequence->GetDataModel()->GetNumberOfFrames();

		FbxTime Start;
		FbxTime End;
		Start.SetSecondDouble(0.0);
		End.SetSecondDouble(Sequence->GetDataModel()->GetFrameRate().AsSeconds(NumFrames));

		return FbxTimeSpan(Start, End);
	}
}

static bool WriteOneFile(const FFofuxoExportRequest& Request, FScopedSlowTask& Progress, FText& OutError)
{
	// An empty list is not an error: it is the "mesh only" request. What can
	// never be missing is the Skeletal Mesh -- the geometry and the skeleton
	// both come from it.
	const bool bMeshOnly = Request.Animations.Num() == 0;

	if (Request.SkeletalMesh == nullptr)
	{
		OutError = LOCTEXT("NoMesh", "Pick the Skeletal Mesh that goes in the file.");
		return false;
	}

	if (Request.Options == nullptr)
	{
		OutError = LOCTEXT("NoOptions", "Export options missing.");
		return false;
	}

	const USkeleton* Skeleton = Request.SkeletalMesh->GetSkeleton();
	for (const UAnimSequence* Sequence : Request.Animations)
	{
		if (Sequence == nullptr)
		{
			OutError = LOCTEXT("NullAnimation", "One of the animations cannot be loaded.");
			return false;
		}

		if (Sequence->GetSkeleton() != Skeleton)
		{
			OutError = FText::Format(
				LOCTEXT("DifferentSkeleton", "The animation {0} belongs to another skeleton, not to {1}'s."),
				FText::FromString(Sequence->GetName()),
				FText::FromString(Request.SkeletalMesh->GetName()));
			return false;
		}
	}

	// The folder may not exist yet.
	const FString Folder = FPaths::GetPath(Request.FilePath);
	if (!Folder.IsEmpty() && !IFileManager::Get().DirectoryExists(*Folder))
	{
		IFileManager::Get().MakeDirectory(*Folder, /*Tree*/ true);
	}

	UnFbx::FFbxExporter* Exporter = UnFbx::FFbxExporter::GetInstance();
	if (Exporter == nullptr)
	{
		OutError = LOCTEXT("NoExporter", "I could not reach the engine's FBX exporter.");
		return false;
	}

	// Without this the engine puts its own options dialog over ours.
	UFbxExportOption* EngineOptions = NewObject<UFbxExportOption>();
	TStrongObjectPtr<UFbxExportOption> Guard(EngineOptions);

	EngineOptions->FbxExportCompatibility = EFbxExportCompatibility::FBX_2020;
	EngineOptions->bASCII = Request.Options->bASCII;
	EngineOptions->bForceFrontXAxis = Request.Options->bFrontOnXAxis;
	// When the mesh goes in the file, this is the field that makes the engine
	// send the morph target curves through its blend shapes instead of turning
	// them into loose properties on the root bone. With no mesh there is no
	// blend shape at all, and morph targets turned on would only dirty the
	// skeleton -- hence the two travelling together.
	EngineOptions->bExportPreviewMesh = Request.Options->bExportMesh;
	EngineOptions->bExportMorphTargets =
		Request.Options->bExportMesh && Request.Options->bExportMorphTargets;
	EngineOptions->bExportLocalTime = true;

	Exporter->SetExportOptionsOverride(EngineOptions);
	ON_SCOPE_EXIT
	{
		Exporter->SetExportOptionsOverride(nullptr);
	};

	const FString MeshName = Request.SkeletalMesh->GetName();

	FbxScene* Scene = nullptr;
	TArray<FbxNode*> Bones;
	TArray<FbxAnimStack*> Stacks;

	if (bMeshOnly)
	{
		Progress.EnterProgressFrame(1.f, FText::Format(
			LOCTEXT("MeshOnly", "Writing the mesh {0}"),
			FText::FromString(MeshName)));

		Exporter->CreateDocument();

		// With no take at all, the writing is the engine's own mesh export --
		// the same one the Content Browser's "Export..." uses. It builds
		// skeleton, bind pose and geometry, and creates no animation stack.
		Exporter->ExportSkeletalMesh(Request.SkeletalMesh);

		Scene = Exporter->*FofuxoPrivate::Steal(FofuxoPrivate::FScene());
		if (Scene == nullptr)
		{
			Exporter->CloseDocument();
			OutError = FText::Format(
				LOCTEXT("MeshOnlyFailed", "The engine could not export the mesh {0}."),
				FText::FromString(MeshName));
			return false;
		}
	}
	else
	{
		Progress.EnterProgressFrame(1.f, FText::Format(
			LOCTEXT("FirstTake", "{0}"),
			FText::FromString(Request.Animations[0]->GetName())));

		Exporter->CreateDocument();

		// The first take goes out through the engine's normal path, and brings
		// the skeleton along -- and, if the mesh is on, the geometry and the
		// bind pose. With it off the engine builds the skeleton and the curves
		// just the same, and hands back the same bone root; it only skips the
		// geometry.
		FbxNode* BoneRoot = Exporter->ExportAnimSequence(
			Request.Animations[0], Request.SkeletalMesh,
			/*bExportSkelMesh*/ Request.Options->bExportMesh, *MeshName);

		if (BoneRoot == nullptr)
		{
			Exporter->CloseDocument();
			OutError = FText::Format(
				LOCTEXT("FirstTakeFailed", "The engine could not export {0}."),
				FText::FromString(Request.Animations[0]->GetName()));
			return false;
		}

		Scene = BoneRoot->GetScene();
		if (Scene == nullptr)
		{
			Exporter->CloseDocument();
			OutError = LOCTEXT("NoScene", "The bone node came back with no scene.");
			return false;
		}

		FString MissingBone;
		if (!FofuxoPrivate::CollectBones(Request.SkeletalMesh, BoneRoot, Bones, MissingBone))
		{
			Exporter->CloseDocument();
			OutError = FText::Format(
				LOCTEXT("MissingBone", "I could not find the bone {0} in the exported hierarchy."),
				FText::FromString(MissingBone));
			return false;
		}

		// The stack the engine created in CreateDocument is called "Unreal
		// Take". It is that name, the same in every file, that makes Blender
		// number the actions.
		FbxAnimStack* FirstStack = Scene->GetSrcObject<FbxAnimStack>(0);
		if (FirstStack == nullptr)
		{
			Exporter->CloseDocument();
			OutError = LOCTEXT("NoStack", "The document came out with no take at all.");
			return false;
		}

		FirstStack->SetName(TCHAR_TO_UTF8(*Request.Animations[0]->GetName()));
		Stacks.Add(FirstStack);

		// From the second take on it is on us: one stack and one layer each.
		for (int32 Index = 1; Index < Request.Animations.Num(); ++Index)
		{
			const UAnimSequence* Sequence = Request.Animations[Index];
			const FString Name = Sequence->GetName();

			Progress.EnterProgressFrame(1.f, FText::FromString(Name));

			// Giving up here leaves no litter: the file is only born in
			// WriteToFile, further down, and the document dies with
			// CloseDocument.
			if (Progress.ShouldCancel())
			{
				Exporter->CloseDocument();
				OutError = LOCTEXT("Cancelled", "Export cancelled. No file was written.");
				return false;
			}

			FbxAnimStack* Stack = FbxAnimStack::Create(Scene, TCHAR_TO_UTF8(*Name));
			FbxAnimLayer* Layer = FbxAnimLayer::Create(Scene, "Base Layer");
			Stack->AddMember(Layer);

			const int32 NumFrames = Sequence->GetDataModel()->GetNumberOfFrames();

			(Exporter->*FofuxoPrivate::Steal(FofuxoPrivate::FExportTake()))(
				Sequence,
				Request.SkeletalMesh,
				Bones,
				Layer,
				FFrameTime(0),
				FFrameTime(NumFrames),
				/*FrameRateScale*/ 1.f,
				/*StartTime*/ 0.f);

			(Exporter->*FofuxoPrivate::Steal(FofuxoPrivate::FFixInterpolation()))(Bones, Layer);

			Stacks.Add(Stack);
		}
	}

	// The engine's SetupAnimStack always writes the span on the document's
	// stack, not on the layer we handed it -- so take 1 ended up with the last
	// animation's duration. Fix them all here, at the end.
	for (int32 Index = 0; Index < Stacks.Num(); ++Index)
	{
		// SetLocalTimeSpan takes a non-const reference, so it needs an lvalue.
		FbxTimeSpan Span = FofuxoPrivate::SpanOf(Request.Animations[Index]);
		Stacks[Index]->SetLocalTimeSpan(Span);
	}

	// Converting the scene and writing it are the two parts that cannot be
	// broken into pieces: hence one single step, announced before it starts.
	Progress.EnterProgressFrame(1.f, bMeshOnly
		? FText::Format(
			LOCTEXT("WritingMeshOnly", "Writing {0}"),
			FText::FromString(FPaths::GetCleanFilename(Request.FilePath)))
		: FText::Format(
			LOCTEXT("Writing", "Writing {0} takes into {1}"),
			FText::AsNumber(Request.Animations.Num()),
			FText::FromString(FPaths::GetCleanFilename(Request.FilePath))));

	// Axis, unit and scale. All three only touch the scene if the target asks
	// for something other than what Unreal already wrote -- on the Blender
	// target, which matches the native export, none of them touches anything.
	{
		FbxAxisSystem::EUpVector Up = FbxAxisSystem::eZAxis;
		switch (Request.Options->UpAxis)
		{
		case EFofuxoAxis::X: Up = FbxAxisSystem::eXAxis; break;
		case EFofuxoAxis::Y: Up = FbxAxisSystem::eYAxis; break;
		default:             Up = FbxAxisSystem::eZAxis; break;
		}

		// Only the up axis is ours; front and handedness stay as Unreal wrote
		// them. Building the whole system by hand flipped the front's sign
		// (Unreal uses a negative eParityOdd), the comparison always came out
		// different, and a DeepConvertScene ran for nothing -- and that is what
		// reached Blender as an axis-permutation rotation.
		const FbxAxisSystem Current = Scene->GetGlobalSettings().GetAxisSystem();

		int32 UpSign = 1;
		const FbxAxisSystem::EUpVector CurrentUp = Current.GetUpVector(UpSign);

		if (CurrentUp != Up)
		{
			int32 FrontSign = 1;
			const FbxAxisSystem::EFrontVector CurrentFront = Current.GetFrontVector(FrontSign);

			// The sign travels inside the enum's value: negative flips the axis.
			const FbxAxisSystem Wanted(
				(FbxAxisSystem::EUpVector)((int32)Up * (UpSign < 0 ? -1 : 1)),
				(FbxAxisSystem::EFrontVector)((int32)CurrentFront * (FrontSign < 0 ? -1 : 1)),
				Current.GetCoorSystem());

			// DeepConvertScene turns the animation curves too, not just the pose.
			FbxAxisSystem Converter = Wanted;
			Converter.DeepConvertScene(Scene);
		}

		// Unit and scale become a single factor: how many centimetres one unit
		// of the file is worth. A scale of 2 wants twice the size, so it divides.
		const double UnitFactor = (Request.Options->Unit == EFofuxoUnit::Metres) ? 100.0 : 1.0;
		const double Factor = UnitFactor / FMath::Max(Request.Options->Scale, UE_KINDA_SMALL_NUMBER);

		const FbxSystemUnit WantedUnit(Factor);
		if (Scene->GetGlobalSettings().GetSystemUnit() != WantedUnit)
		{
			WantedUnit.ConvertScene(Scene);
		}
	}

	// WriteToFile closes the document by itself.
	Exporter->WriteToFile(*Request.FilePath);

	if (IFileManager::Get().FileSize(*Request.FilePath) <= 0)
	{
		OutError = FText::Format(
			LOCTEXT("FileNotWritten", "The file {0} was not written."),
			FText::FromString(Request.FilePath));
		return false;
	}

	return true;
}

bool FFofuxoFbxWriter::Export(const FFofuxoExportRequest& Request, FText& OutError)
{
	if (Request.Options == nullptr)
	{
		OutError = LOCTEXT("NoBatchOptions", "Export options missing.");
		return false;
	}

	const bool bMeshOnly = Request.Animations.Num() == 0;

	if (bMeshOnly && !Request.Options->bExportMesh)
	{
		OutError = LOCTEXT("NothingAtAll",
			"With no animation ticked and \"Export the mesh\" off there is nothing left to write. "
			"Tick an animation, or turn the mesh on.");
		return false;
	}

	const TArray<FFofuxoBatch> Batches = FofuxoSliceIntoBatches(
		Request.Animations, Request.Options->TakesPerFile, Request.FilePath);

	// One step per take, plus one per file for the writing. Without this the
	// editor goes through the whole export without painting a frame, and Windows
	// marks the window as "not responding" -- which is exactly what a hang looks
	// like. Mesh only has no take, but it does have the geometry conversion: it
	// counts as one.
	FScopedSlowTask Progress(
		(bMeshOnly ? 1 : Request.Animations.Num()) + Batches.Num(),
		FText::Format(LOCTEXT("Progress", "{0} -- Export"), Fofuxo::ShortName()));

	Progress.MakeDialog(/*bShowCancelButton*/ true);

	for (const FFofuxoBatch& Batch : Batches)
	{
		// Each batch is a whole, independent export: new document, new scene,
		// file closed at the end. That is where the memory ceiling comes from --
		// what has already been written is no longer in hand.
		FFofuxoExportRequest Part = Request;
		Part.Animations = Batch.Animations;
		Part.FilePath = Batch.Path;

		if (!WriteOneFile(Part, Progress, OutError))
		{
			return false;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
