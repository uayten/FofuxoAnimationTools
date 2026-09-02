// Fofuxo

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/Object.h"

#include "FofuxoExportOptions.generated.h"

class USkeletalMesh;

UENUM()
enum class EFofuxoAxis : uint8
{
	Z UMETA(DisplayName = "Z (Blender, Unreal)"),
	Y UMETA(DisplayName = "Y (Unity, Maya)"),
	X UMETA(DisplayName = "X"),
};

/**
 * What goes on disk.
 *
 * FBX is the only one Unreal and Unity read with nothing installed, and it holds
 * each animation as a take.
 *
 * USD is open (Pixar, now AOUSD) and holds each animation as a SkelAnimation
 * prim hanging off a single skeleton. The file comes out several times smaller,
 * but readers tend to play only the animation bound to the skeleton: as a clip
 * library it depends on the importer at the other end.
 *
 * glTF 2.0 is open (Khronos) and has a native "animations" array, with names,
 * over a single "skin" -- which is exactly "one skeleton, several animations".
 * The importer ships inside Blender and creates one action for each. It is
 * always metres and Y-up, so there is no unit to choose; the up axis still picks
 * which way the rig faces -- see the Target below.
 */
UENUM()
enum class EFofuxoFormat : uint8
{
	FBX UMETA(DisplayName = "FBX (Autodesk)"),
	USD UMETA(DisplayName = "USD (open)"),
	GLTF UMETA(DisplayName = "glTF 2.0 (open)"),
};

UENUM()
enum class EFofuxoUnit : uint8
{
	Centimetres UMETA(DisplayName = "Centimetres"),
	Metres UMETA(DisplayName = "Metres"),
};

/** A way of exporting, with a name. Blender and Unity ship ready; the rest is yours. */
USTRUCT()
struct FFofuxoTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Target")
	FString Name = TEXT("My target");

	UPROPERTY(EditAnywhere, Category = "Target")
	EFofuxoAxis UpAxis = EFofuxoAxis::Z;

	UPROPERTY(EditAnywhere, Category = "Target", meta = (DisplayName = "Front on the X axis"))
	bool bFrontOnXAxis = false;

	UPROPERTY(EditAnywhere, Category = "Target")
	EFofuxoUnit Unit = EFofuxoUnit::Centimetres;

	/** Multiplies on top of the unit. 2 comes out twice the size. */
	UPROPERTY(EditAnywhere, Category = "Target", meta = (ClampMin = "0.0001", UIMin = "0.01", UIMax = "100.0"))
	float Scale = 1.f;
};

/**
 * The export window's options.
 *
 * Whatever is marked "config" is remembered between sessions -- including the
 * targets you create. The rest comes from the selection every time the window
 * opens.
 */
UCLASS(config = EditorPerProjectUserSettings)
class UFofuxoExportOptions : public UObject
{
	GENERATED_BODY()

public:

	static const TCHAR* BlenderTarget;
	static const TCHAR* UnityTarget;

	/** Blender, Unity, or one of yours from "My targets". */
	UPROPERTY(EditAnywhere, config, Category = "Target", meta = (GetOptions = "GetTargetNames"))
	FString Target;

	UPROPERTY(EditAnywhere, config, Category = "File", meta = (DisplayName = "Format"))
	EFofuxoFormat Format = EFofuxoFormat::FBX;

	UPROPERTY(EditAnywhere, config, Category = "File", meta = (DisplayName = "Folder"))
	FDirectoryPath Folder;

	/**
	 * Applies to every format. It greys out with "Animations per file" at 1:
	 * then each file carries the name of the animation inside it.
	 */
	UPROPERTY(EditAnywhere, Category = "File", meta = (DisplayName = "File name", EditCondition = "TakesPerFile != 1"))
	FString FileName;

	/**
	 * How many animations per file. 0 puts them all in the same one.
	 *
	 * It exists because a single file doesn't scale: the animations all stay
	 * alive in the same scene until the end, and writing is one call that can be
	 * neither split nor cancelled. Measured here in FBX with 477 takes: 8.1 GB
	 * peak and a 690 MB file. In batches the peak falls in proportion.
	 *
	 * Applies to every format. In USD, each file is a stage with N animations
	 * hanging off the same skeleton -- 1 gives back one animation per file.
	 */
	UPROPERTY(EditAnywhere, config, Category = "File", meta = (DisplayName = "Animations per file", ClampMin = "0", UIMin = "0", UIMax = "500"))
	int32 TakesPerFile = 100;

	/** The skeleton and the mesh that go in the file. Every animation has to belong to this skeleton. */
	UPROPERTY(EditAnywhere, Category = "Content", meta = (DisplayName = "Skeletal Mesh"))
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	// The four fields below mirror the chosen target. On the ready-made targets
	// they stay locked -- Blender's exists precisely to come out identical to
	// what Unreal already writes, and touching it would break that. Pick a
	// target of your own and they unlock.

	UPROPERTY(EditAnywhere, Category = "Advanced", meta = (DisplayName = "Up axis", EditCondition = "bTargetIsMine"))
	EFofuxoAxis UpAxis = EFofuxoAxis::Z;

	UPROPERTY(EditAnywhere, Category = "Advanced", meta = (DisplayName = "Front on the X axis", EditCondition = "bTargetIsMine && Format == EFofuxoFormat::FBX"))
	bool bFrontOnXAxis = false;

	UPROPERTY(EditAnywhere, Category = "Advanced", meta = (DisplayName = "Unit", EditCondition = "bTargetIsMine && Format != EFofuxoFormat::GLTF"))
	EFofuxoUnit Unit = EFofuxoUnit::Centimetres;

	UPROPERTY(EditAnywhere, Category = "Advanced", meta = (DisplayName = "Scale", EditCondition = "bTargetIsMine", ClampMin = "0.0001", UIMin = "0.01", UIMax = "100.0"))
	float Scale = 1.f;

	/**
	 * Whether the mesh goes along with the animations. Applies to every format.
	 *
	 * Turned off, only the skeleton and the curves come out: the file is far
	 * smaller and the export faster, because converting heavy geometry is what
	 * costs most. It is for when the other side already has the good mesh -- and
	 * it is worth remembering that a mesh that went through Unreal comes back
	 * triangulated anyway, so the original is almost always better than the
	 * exported one.
	 *
	 * It is also the shape Unity expects for a clip: an FBX with the mesh and
	 * the rig, which produces the Avatar, and one file per animation with
	 * Animation Type = Generic or Humanoid and Avatar Definition = Copy From
	 * Other Avatar. The bone hierarchy comes out the same in both, and that is
	 * what makes the copy fit.
	 *
	 * With no animation ticked, turning this off leaves nothing in the file --
	 * and then the Export button greys out.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Content", meta = (DisplayName = "Export the mesh"))
	bool bExportMesh = true;

	/** Create them here, and the name shows up in the Target list up top. */
	UPROPERTY(EditAnywhere, config, AdvancedDisplay, Category = "Advanced", meta = (DisplayName = "My targets", TitleProperty = "Name"))
	TArray<FFofuxoTarget> MyTargets;

	/**
	 * The mesh's blend shapes. Off by default for two reasons: the engine writes
	 * the morph curves only once, along with the mesh, so they would only apply
	 * to the first take; and Better FBX creates a shape key action with the same
	 * name as the pose action, which makes Blender duplicate everything with a
	 * .001 suffix.
	 *
	 * With no mesh there is no blend shape to hang the curves off, which is why
	 * this field follows "Export the mesh".
	 */
	UPROPERTY(EditAnywhere, config, AdvancedDisplay, Category = "Advanced", meta = (DisplayName = "Export morph targets", EditCondition = "Format == EFofuxoFormat::FBX && bExportMesh"))
	bool bExportMorphTargets = false;

	/**
	 * Text instead of binary, in every format: FBX ASCII on one side, .usda and
	 * .gltf on the others. It is for opening the file in a text editor and
	 * seeing what came out -- and it is the only way to inspect a USD, which by
	 * default comes out in the compressed binary crate. The file ends up several
	 * times larger.
	 */
	UPROPERTY(EditAnywhere, config, AdvancedDisplay, Category = "Advanced", meta = (DisplayName = "Export as text (ASCII)"))
	bool bASCII = false;

	/**
	 * The paths of the animations you unticked, so the window remembers next
	 * time. It stores the unticked ones and not the ticked ones so that a new
	 * animation shows up ticked, which is the useful default.
	 */
	UPROPERTY(config)
	TArray<FString> Unticked;

	/** Whether the animation list opens expanded or collapsed. */
	UPROPERTY(config)
	bool bListExpanded = true;

	/** The height you left the list at, by dragging the little bar under it. */
	UPROPERTY(config)
	float ListHeight = 300.f;

	/** Only there for the mirrored fields' EditCondition. */
	UPROPERTY(Transient)
	bool bTargetIsMine = false;

	UFUNCTION()
	TArray<FString> GetTargetNames() const;

	/** Copies the chosen target into the mirrored fields. */
	void ApplyTarget();

	/** Gives the mirrored fields back to the target, when the target is yours. */
	void WriteToTarget();

	/** The chosen format's extension, with the dot. */
	FString Extension() const;

	/**
	 * The full path, with the format's extension. With Total greater than one it
	 * numbers: Thing_01.fbx, Thing_02.fbx.
	 */
	FString BuildPath(int32 Index = 0, int32 Total = 1) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
