// Fofuxo
//
// The same export as the window, from the command line. It is for exporting a
// whole folder without clicking anything, and it is the way to run the plugin
// with the editor in -unattended mode.

#include "FofuxoExportOptions.h"
#include "FofuxoExportRequest.h"
#include "FofuxoFbxWriter.h"
#include "FofuxoSceneWriter.h"

#include "Animation/AnimSequence.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoExporter, Log, All);

static void FofuxoExportByCommand(const TArray<FString>& Arguments)
{
	// Without the animation folder only the mesh comes out -- the same as the
	// window does with no animation ticked.
	if (Arguments.Num() < 2)
	{
		UE_LOG(LogFofuxoExporter, Error,
			TEXT("Usage: Fofuxo.Export <output.fbx> <mesh path> [animation folder] [Unity]"));
		return;
	}

	const FString Output = Arguments[0];
	const FString MeshPath = Arguments[1];
	const FString AnimationFolder = Arguments.Num() > 2 ? Arguments[2] : FString();

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (Mesh == nullptr)
	{
		UE_LOG(LogFofuxoExporter, Error, TEXT("I could not load the Skeletal Mesh %s"), *MeshPath);
		return;
	}

	TArray<UAnimSequence*> Animations;

	if (!AnimationFolder.IsEmpty())
	{
		IAssetRegistry& Registry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		// Only scan if the registry hasn't finished building itself. Always
		// scanning, synchronously, forces a reread of every .uasset header in
		// the project -- in a freshly opened editor that alone costs minutes,
		// and in an editor that is already up it changes nothing, because the
		// registry is already done.
		if (Registry.IsLoadingAssets())
		{
			Registry.SearchAllAssets(/*bSynchronousSearch*/ true);
		}

		FARFilter Filter;
		Filter.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(FName(*AnimationFolder));
		Filter.bRecursivePaths = true;

		TArray<FAssetData> Found;
		Registry.GetAssets(Filter, Found);

		for (const FAssetData& Asset : Found)
		{
			if (UAnimSequence* Sequence = Cast<UAnimSequence>(Asset.GetAsset()))
			{
				if (Sequence->GetSkeleton() == Mesh->GetSkeleton())
				{
					Animations.Add(Sequence);
				}
			}
		}

		if (Animations.Num() == 0)
		{
			UE_LOG(LogFofuxoExporter, Error,
				TEXT("No Animation Sequence of %s's skeleton in %s"), *Mesh->GetName(), *AnimationFolder);
			return;
		}

		Animations.Sort([](const UAnimSequence& A, const UAnimSequence& B)
		{
			return A.GetName() < B.GetName();
		});
	}
	else
	{
		UE_LOG(LogFofuxoExporter, Display,
			TEXT("No animation folder: exporting only the mesh %s"), *Mesh->GetName());
	}

	UFofuxoExportOptions* Options = NewObject<UFofuxoExportOptions>();
	TStrongObjectPtr<UFofuxoExportOptions> Guard(Options);

	// It also accepts the name of a target of yours, saved by the window.
	Options->LoadConfig();
	Options->Target = Arguments.Num() > 3 ? Arguments[3] : UFofuxoExportOptions::BlenderTarget;
	Options->ApplyTarget();

	UE_LOG(LogFofuxoExporter, Display, TEXT("Targets of yours loaded: %d"), Options->MyTargets.Num());
	for (const FFofuxoTarget& Mine : Options->MyTargets)
	{
		UE_LOG(LogFofuxoExporter, Display, TEXT("  \"%s\" axis=%d unit=%d scale=%f"),
			*Mine.Name, (int32)Mine.UpAxis, (int32)Mine.Unit, Mine.Scale);
	}
	UE_LOG(LogFofuxoExporter, Display, TEXT("Applied \"%s\" (yours=%d) axis=%d unit=%d scale=%f"),
		*Options->Target, Options->bTargetIsMine ? 1 : 0,
		(int32)Options->UpAxis, (int32)Options->Unit, Options->Scale);

	FFofuxoExportRequest Request;
	Request.Animations = Animations;
	Request.SkeletalMesh = Mesh;
	Request.FilePath = Output;
	Request.Options = Options;

	// The format comes from the ini, same as the window's -- the command exists
	// to repeat what the window does, not to have a rule of its own.
	FText Error;

	const bool bWorked = Options->Format == EFofuxoFormat::FBX
		? FFofuxoFbxWriter::Export(Request, Error)
		: FFofuxoSceneWriter::Export(Request, Error);

	if (!bWorked)
	{
		UE_LOG(LogFofuxoExporter, Error, TEXT("Failed: %s"), *Error.ToString());
		return;
	}

	if (Animations.Num() == 0)
	{
		UE_LOG(LogFofuxoExporter, Display, TEXT("Fofuxo: only the mesh %s written to %s"), *Mesh->GetName(), *Output);
	}
	else
	{
		UE_LOG(LogFofuxoExporter, Display,
			TEXT("Fofuxo: %d animations written starting from %s"), Animations.Num(), *Output);
	}
}

static FAutoConsoleCommand GFofuxoExport(
	TEXT("Fofuxo.Export"),
	TEXT("Fofuxo.Export <output.fbx> <mesh path> [animation folder] [Unity]"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&FofuxoExportByCommand));
