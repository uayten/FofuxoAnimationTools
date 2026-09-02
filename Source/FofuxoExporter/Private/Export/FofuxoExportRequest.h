// Fofuxo -- what the window asks the writers for

#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class UFofuxoExportOptions;
class USkeletalMesh;

/**
 * One export, as the window and the console command state it.
 *
 * It lives in its own header, and not in the FBX writer's, because all three
 * formats take it: leaving it there made the scene writer include the FBX
 * writer's header and look like it depended on FBX.
 */
struct FFofuxoExportRequest
{
	/** One animation becomes one take. The order here is the order in the file. */
	TArray<UAnimSequence*> Animations;

	USkeletalMesh* SkeletalMesh = nullptr;

	FString FilePath;

	const UFofuxoExportOptions* Options = nullptr;
};
