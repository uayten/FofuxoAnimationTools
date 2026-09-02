// Fofuxo

#include "FofuxoSceneWriter.h"
#include "FofuxoName.h"

#include "FofuxoExportBatch.h"
#include "FofuxoExportOptions.h"
#include "FofuxoExportRequest.h"
#include "FofuxoSceneWriters.h"

#include "Animation/AnimSequence.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "FofuxoExporter"

bool FFofuxoSceneWriter::Export(const FFofuxoExportRequest& Request, FText& OutError)
{
	if (Request.Options == nullptr)
	{
		OutError = LOCTEXT("SceneNoOptions", "Export options missing.");
		return false;
	}

	// An empty list is the "mesh only" request. Here, unlike FBX, the mesh can
	// be turned off -- and with no mesh and no animation there is no file left.
	const bool bMeshOnly = Request.Animations.Num() == 0;

	if (bMeshOnly && !Request.Options->bExportMesh)
	{
		OutError = LOCTEXT("SceneNothingAtAll",
			"With no animation ticked and \"Export the mesh\" off there is nothing left to write. "
			"Tick an animation, or turn the mesh on.");
		return false;
	}

	const bool bGltf = Request.Options->Format == EFofuxoFormat::GLTF;

	FFofuxoWriteScene& Writer = bGltf ? FofuxoGltfSceneWriter() : FofuxoUsdSceneWriter();
	if (!Writer.IsBound())
	{
		OutError = bGltf
			? LOCTEXT("GltfNoModule",
				"The glTF scene module did not load. Turn the \"glTF Exporter\" plugin on in Edit > Plugins, "
				"restart the editor, and try again.")
			: LOCTEXT("UsdNoModule",
				"The USD scene module did not load. Turn the \"USD Importer\" plugin on in Edit > Plugins, "
				"restart the editor, and try again.");
		return false;
	}

	const FString Folder = FPaths::GetPath(Request.FilePath);
	if (!Folder.IsEmpty() && !IFileManager::Get().DirectoryExists(*Folder))
	{
		IFileManager::Get().MakeDirectory(*Folder, /*Tree*/ true);
	}

	// The same slicing as FBX, from the same field: 0 puts everything in one
	// file, and each batch becomes a stage with N animations inside. 1 gives
	// back one animation per file.
	const TArray<FFofuxoBatch> Batches = FofuxoSliceIntoBatches(
		Request.Animations, Request.Options->TakesPerFile, Request.FilePath);

	FScopedSlowTask Progress(bMeshOnly ? 1 : Request.Animations.Num(), FText::Format(
		bGltf
			? LOCTEXT("GltfProgress", "{0} -- Export (glTF)")
			: LOCTEXT("UsdProgress", "{0} -- Export (USD)"),
		Fofuxo::ShortName()));
	Progress.MakeDialog(/*bShowCancelButton*/ true);

	for (int32 Index = 0; Index < Batches.Num(); ++Index)
	{
		const FFofuxoBatch& Batch = Batches[Index];

		FFofuxoSceneRequest Scene;
		Scene.Animations = Batch.Animations;
		Scene.Mesh = Request.SkeletalMesh;
		Scene.Path = Batch.Path;

		// The same Target that rules FBX rules here -- in USD. USD only has Y
		// and Z up; a target of yours on X falls back to Z, which is what Unreal
		// writes. glTF ignores the unit: it is always metres.
		const double Base = (Request.Options->Unit == EFofuxoUnit::Metres) ? 1.0 : 0.01;
		Scene.MetresPerUnit = Base * FMath::Max(Request.Options->Scale, UE_KINDA_SMALL_NUMBER);
		Scene.bYUp = Request.Options->UpAxis == EFofuxoAxis::Y;
		Scene.Scale = FMath::Max(Request.Options->Scale, UE_KINDA_SMALL_NUMBER);
		Scene.bWithMesh = Request.Options->bExportMesh;
		// The axis rules glTF too, only differently: there it does not choose
		// which way is up -- that is always Y -- but whether the rig comes out
		// turned like the FBX's. A target that asks for Y up is a target that
		// receives FBX with the half turn, and the two formats have to come out
		// alike for a character from one to take an animation from the other.
		Scene.bLikeTheFbx = bGltf && Request.Options->UpAxis == EFofuxoAxis::Y;

		Progress.EnterProgressFrame(bMeshOnly ? 1.f : Batch.Animations.Num(), bMeshOnly
			? FText::Format(
				LOCTEXT("SceneMeshOnly", "Writing {0}"),
				FText::FromString(FPaths::GetCleanFilename(Scene.Path)))
			: FText::Format(
				LOCTEXT("SceneWriting", "Writing {0} animations into {1}"),
				FText::AsNumber(Batch.Animations.Num()),
				FText::FromString(FPaths::GetCleanFilename(Scene.Path))));

		// Cancelling applies between files: inside a stage the writing is one
		// single call, as it is in FBX.
		if (Progress.ShouldCancel())
		{
			OutError = FText::Format(
				LOCTEXT("SceneCancelled", "Export cancelled. The {0} files already written stayed in the folder."),
				FText::AsNumber(Index));
			return false;
		}

		if (!Writer.Execute(Scene, OutError))
		{
			return false;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
