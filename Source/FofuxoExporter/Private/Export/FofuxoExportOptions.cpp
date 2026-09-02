// Fofuxo

#include "FofuxoExportOptions.h"

#include "Misc/Paths.h"

const TCHAR* UFofuxoExportOptions::BlenderTarget = TEXT("Blender");
const TCHAR* UFofuxoExportOptions::UnityTarget = TEXT("Unity");

TArray<FString> UFofuxoExportOptions::GetTargetNames() const
{
	TArray<FString> Names;
	Names.Add(BlenderTarget);
	Names.Add(UnityTarget);

	for (const FFofuxoTarget& Mine : MyTargets)
	{
		// A target with no name could never be picked again, and a repeated name
		// would point at two places.
		if (!Mine.Name.IsEmpty() && !Names.Contains(Mine.Name))
		{
			Names.Add(Mine.Name);
		}
	}

	return Names;
}

void UFofuxoExportOptions::ApplyTarget()
{
	if (Target.IsEmpty())
	{
		Target = BlenderTarget;
	}

	for (const FFofuxoTarget& Mine : MyTargets)
	{
		if (Mine.Name == Target)
		{
			UpAxis = Mine.UpAxis;
			bFrontOnXAxis = Mine.bFrontOnXAxis;
			Unit = Mine.Unit;
			Scale = Mine.Scale;
			bTargetIsMine = true;
			return;
		}
	}

	bTargetIsMine = false;

	if (Target == UnityTarget)
	{
		// Unity converts the unit by itself on import, but not the axis.
		UpAxis = EFofuxoAxis::Y;
		bFrontOnXAxis = false;
		Unit = EFofuxoUnit::Centimetres;
		Scale = 1.f;
		return;
	}

	// Blender, and also the case of a name that no longer exists.
	//
	// Centimetres again. Metres looked like the way for the object to come in at
	// scale 1 in Blender, and they are not: the FBX SDK's ConvertScene doesn't
	// touch the vertices, it puts the scale on the nodes -- so the 0.01 showed
	// up on the object all the same, only now coming from inside the file. With
	// centimetres the file comes out identical to Unreal's native export, and
	// the importer is the one deciding the object's scale.
	Target = BlenderTarget;
	UpAxis = EFofuxoAxis::Z;
	bFrontOnXAxis = false;
	Unit = EFofuxoUnit::Centimetres;
	Scale = 1.f;
}

void UFofuxoExportOptions::WriteToTarget()
{
	if (!bTargetIsMine)
	{
		return;
	}

	for (FFofuxoTarget& Mine : MyTargets)
	{
		if (Mine.Name == Target)
		{
			Mine.UpAxis = UpAxis;
			Mine.bFrontOnXAxis = bFrontOnXAxis;
			Mine.Unit = Unit;
			Mine.Scale = Scale;
			return;
		}
	}
}

FString UFofuxoExportOptions::Extension() const
{
	if (Format == EFofuxoFormat::USD)
	{
		// In USD it is the extension, and not an exporter option, that decides
		// text or binary: .usda is text, .usd falls into the binary crate.
		return bASCII ? TEXT(".usda") : TEXT(".usd");
	}

	if (Format == EFofuxoFormat::GLTF)
	{
		// Same idea: .glb is a single binary file, .gltf is readable JSON with
		// the data in a .bin next to it.
		return bASCII ? TEXT(".gltf") : TEXT(".glb");
	}

	return TEXT(".fbx");
}

FString UFofuxoExportOptions::BuildPath(int32 Index, int32 Total) const
{
	FString Name = FileName;
	if (Name.IsEmpty())
	{
		Name = TEXT("Exported");
	}

	// Strip the extension the person may have typed: otherwise switching format
	// produces "Thing.fbx.usd", and the batch becomes "Thing.fbx_01.fbx".
	if (Name.EndsWith(TEXT(".fbx"), ESearchCase::IgnoreCase)
		|| Name.EndsWith(TEXT(".usd"), ESearchCase::IgnoreCase)
		|| Name.EndsWith(TEXT(".usda"), ESearchCase::IgnoreCase)
		|| Name.EndsWith(TEXT(".usdc"), ESearchCase::IgnoreCase)
		|| Name.EndsWith(TEXT(".glb"), ESearchCase::IgnoreCase)
		|| Name.EndsWith(TEXT(".gltf"), ESearchCase::IgnoreCase))
	{
		Name = FPaths::GetBaseFilename(Name);
	}

	// A single file stays unnumbered -- always numbering messed up anyone with a
	// pipeline pointing at the bare name.
	if (Total > 1)
	{
		Name += FString::Printf(TEXT("_%02d"), Index + 1);
	}

	return FPaths::Combine(Folder.Path, Name + Extension());
}

#if WITH_EDITOR
void UFofuxoExportOptions::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName Changed = PropertyChangedEvent.GetPropertyName();

	static const TSet<FName> Mirrored = {
		GET_MEMBER_NAME_CHECKED(UFofuxoExportOptions, UpAxis),
		GET_MEMBER_NAME_CHECKED(UFofuxoExportOptions, bFrontOnXAxis),
		GET_MEMBER_NAME_CHECKED(UFofuxoExportOptions, Unit),
		GET_MEMBER_NAME_CHECKED(UFofuxoExportOptions, Scale),
	};

	if (Changed == GET_MEMBER_NAME_CHECKED(UFofuxoExportOptions, Target))
	{
		ApplyTarget();
	}
	else if (Mirrored.Contains(Changed))
	{
		WriteToTarget();
	}
	else if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UFofuxoExportOptions, MyTargets))
	{
		// Renaming or deleting a target may have orphaned the chosen one;
		// ApplyTarget falls back to Blender when it can't find the name.
		ApplyTarget();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
