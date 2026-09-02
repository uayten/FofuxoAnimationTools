// Fofuxo -- glTF scene
//
// Writes a skeleton and several animations into a single glTF file.
//
// Here the format does the work: glTF has a native "animations" array, with
// names, over a single "skin". It is not a library the other side needs to know
// how to interpret -- it is the format's own design, and the importer that ships
// inside Blender creates one action for each.
//
// The engine's exporter writes one animation per file, but by its own choice:
// the builder takes as many as you add. That is what is done here.

#include "FofuxoSceneWriters.h"
#include "FofuxoGltfHalfTurn.h"

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Json/GLTFJsonNode.h"
#include "Json/GLTFJsonScene.h"
#include "Json/GLTFJsonSkin.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Options/GLTFExportOptions.h"
#include "UObject/GCObjectScopeGuard.h"

#define LOCTEXT_NAMESPACE "FofuxoGltfScene"

namespace FofuxoGltf
{
	static bool Write(const FFofuxoSceneRequest& Request, FText& OutError)
	{
		// An empty list gives a file with just the skin and the mesh, and the
		// "animations" array doesn't even show up -- which is the "mesh only"
		// request.
		if (Request.Mesh == nullptr)
		{
			OutError = LOCTEXT("GltfNoMesh", "Without a Skeletal Mesh there is no writing the skeleton.");
			return false;
		}

		UGLTFExportOptions* Options = NewObject<UGLTFExportOptions>();
		FGCObjectScopeGuard Guard(Options);

		// glTF is always metres and Y-up -- there is no axis and no unit to
		// choose, and that is exactly why it travels well. The 0.01 is the
		// conversion from Unreal's centimetre to the metre; the target's scale
		// multiplies on top.
		Options->ExportUniformScale = 0.01f * static_cast<float>(Request.Scale);
		Options->bExportVertexSkinWeights = true;
		Options->bExportAnimationSequences = Request.Animations.Num() > 0;

		FFofuxoGltfBuilder Builder(FPaths::GetCleanFilename(Request.Path), Options);
		Builder.ClearLog();

		FGLTFJsonNode* Node = Builder.AddNode();
		if (Node == nullptr)
		{
			OutError = LOCTEXT("GltfNoNode", "I could not create the glTF's root node.");
			return false;
		}

		Node->Name = Request.Mesh->GetName();

		// The skin comes out once, and every animation hangs off it. This is the
		// "one skeleton, several animations" that FBX does with takes.
		Node->Skin = Builder.AddUniqueSkin(Node, Request.Mesh);
		if (Node->Skin == nullptr)
		{
			OutError = FText::Format(
				LOCTEXT("GltfNoSkin", "The engine did not convert {0}'s skeleton."),
				FText::FromString(Request.Mesh->GetName()));
			return false;
		}

		if (Request.bWithMesh)
		{
			Node->Mesh = Builder.AddUniqueMesh(Request.Mesh);
		}

		for (UAnimSequence* Sequence : Request.Animations)
		{
			if (Sequence == nullptr)
			{
				continue;
			}

			if (Builder.AddUniqueAnimation(Node, Request.Mesh, Sequence) == nullptr)
			{
				OutError = FText::Format(
					LOCTEXT("GltfAnimationFailed", "The engine did not convert the animation {0}."),
					FText::FromString(Sequence->GetName()));
				return false;
			}
		}

		FGLTFJsonScene* Scene = Builder.AddScene();
		if (Scene == nullptr)
		{
			OutError = LOCTEXT("GltfNoScene", "I could not create the glTF's scene.");
			return false;
		}

		Scene->Nodes.Add(Node);
		Builder.DefaultScene = Scene;

		// The builder has a task queue with progress of its own -- that is why
		// the heavy conversion here doesn't leave the editor mute the way FBX
		// used to.
		Builder.ProcessSlowTasks();

		// A target that wants the rig to match the FBX: two corrections, and both
		// only here -- on the Blender target the file keeps coming out as the
		// engine's exporter writes it, which is what Blender's importer expects.
		if (Request.bLikeTheFbx)
		{
			// The first is about shape. FBX hangs the skeleton and the mesh side
			// by side, at the scene's root; the engine's exporter hangs the
			// skeleton inside the mesh's node. In Unity that becomes one extra
			// GameObject in every bone's path, and a Unity clip addresses bones
			// by path: "DEF-Root/DEF-pelvis" on one side does not find
			// "Mesh/DEF-Root/DEF-pelvis" on the other.
			FGLTFJsonNode* BoneRoot = Node->Skin->Joints.Num() > 0 ? Node->Skin->Joints[0] : nullptr;
			if (BoneRoot != nullptr && Node->Children.Remove(BoneRoot) > 0)
			{
				Scene->Nodes.Add(BoneRoot);

				// The skin's "skeleton" points at the top of the skeleton, and
				// the top is no longer the mesh's node.
				Node->Skin->Skeleton = BoneRoot;
			}

			// The second is about axis, and it is explained in FofuxoGltfHalfTurn.h.
			if (!Builder.ApplyHalfTurn(OutError))
			{
				return false;
			}
		}

		if (!Builder.WriteAllFiles(FPaths::GetPath(Request.Path)))
		{
			OutError = FText::Format(
				LOCTEXT("GltfNotWritten", "The file {0} was not written. The Output Log has the reason."),
				FText::FromString(FPaths::GetCleanFilename(Request.Path)));
			return false;
		}

		return true;
	}
}

class FFofuxoGltfSceneModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
		FofuxoGltfSceneWriter().BindStatic(&FofuxoGltf::Write);
	}

	virtual void ShutdownModule() override
	{
		FofuxoGltfSceneWriter().Unbind();
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFofuxoGltfSceneModule, FofuxoGltfScene)
