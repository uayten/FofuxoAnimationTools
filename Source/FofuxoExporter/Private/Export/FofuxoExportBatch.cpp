// Fofuxo -- slicing an export into files

#include "FofuxoExportBatch.h"

#include "Animation/AnimSequence.h"
#include "Misc/Paths.h"

namespace
{
	/**
	 * A batch's path: the caller's own, with _01, _02 before the extension.
	 * A total of one or less gives the path back untouched.
	 *
	 * With OwnName filled in, it wins over the numbering.
	 */
	FString PathOfBatch(const FString& Base, int32 Index, int32 Total, const FString& OwnName)
	{
		if (!OwnName.IsEmpty())
		{
			return FPaths::Combine(FPaths::GetPath(Base), OwnName + TEXT(".") + FPaths::GetExtension(Base));
		}

		if (Total <= 1)
		{
			return Base;
		}

		return FPaths::Combine(
			FPaths::GetPath(Base),
			FPaths::GetBaseFilename(Base) + FString::Printf(TEXT("_%02d."), Index + 1) + FPaths::GetExtension(Base));
	}
}

TArray<FFofuxoBatch> FofuxoSliceIntoBatches(
	const TArray<UAnimSequence*>& Animations,
	int32 TakesPerFile,
	const FString& BasePath)
{
	TArray<FFofuxoBatch> Batches;

	const int32 Total = Animations.Num();

	// No animation at all comes out as one file with the mesh and the skeleton
	// -- the "mesh only" request. There is nothing to slice.
	if (Total == 0)
	{
		FFofuxoBatch Only;
		Only.Path = BasePath;
		Batches.Add(MoveTemp(Only));
		return Batches;
	}

	const int32 PerFile = TakesPerFile > 0 ? FMath::Min(TakesPerFile, Total) : Total;
	const int32 NumFiles = FMath::DivideAndRoundUp(Total, PerFile);

	Batches.Reserve(NumFiles);

	for (int32 File = 0; File < NumFiles; ++File)
	{
		const int32 First = File * PerFile;
		const int32 HowMany = FMath::Min(PerFile, Total - First);

		FFofuxoBatch Batch;
		Batch.Animations = TArray<UAnimSequence*>(Animations.GetData() + First, HowMany);

		// One take per file: the file takes the animation's name. This only holds
		// when that is what was asked for -- the remainder of a batch of 25 stays
		// numbered along with its siblings.
		const FString OwnName = (PerFile == 1 && Batch.Animations.Num() == 1 && Batch.Animations[0] != nullptr)
			? Batch.Animations[0]->GetName()
			: FString();

		Batch.Path = PathOfBatch(BasePath, File, NumFiles, OwnName);

		Batches.Add(MoveTemp(Batch));
	}

	return Batches;
}
