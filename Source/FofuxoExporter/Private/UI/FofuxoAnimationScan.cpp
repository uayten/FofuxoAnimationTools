// Fofuxo -- finding a skeleton's animations

#include "FofuxoAnimationScan.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Modules/ModuleManager.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "FofuxoExporter"

TArray<UAnimSequence*> FofuxoAnimationsOfSkeleton(const USkeleton* Skeleton)
{
	TArray<UAnimSequence*> Found;
	if (Skeleton == nullptr)
	{
		return Found;
	}

	IAssetRegistry& Registry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> Candidates;
	Registry.GetAssetsByClass(UAnimSequence::StaticClass()->GetClassPathName(), Candidates, /*bSearchSubClasses*/ true);

	const FString SkeletonName = FAssetData(Skeleton).GetExportTextName();

	// Filter first, load second: GetAsset is the expensive part, and the
	// progress count only makes sense over what is actually going to be loaded.
	TArray<FAssetData> Ours;
	for (const FAssetData& Asset : Candidates)
	{
		if (Asset.GetTagValueRef<FString>(TEXT("Skeleton")) == SkeletonName)
		{
			Ours.Add(Asset);
		}
	}

	FScopedSlowTask Progress(Ours.Num(), LOCTEXT("Gathering", "Gathering the skeleton's animations"));
	if (Ours.Num() > 8)
	{
		Progress.MakeDialog();
	}

	for (const FAssetData& Asset : Ours)
	{
		Progress.EnterProgressFrame(1.f, FText::FromName(Asset.AssetName));

		if (UAnimSequence* Sequence = Cast<UAnimSequence>(Asset.GetAsset()))
		{
			Found.Add(Sequence);
		}
	}

	return Found;
}

#undef LOCTEXT_NAMESPACE
