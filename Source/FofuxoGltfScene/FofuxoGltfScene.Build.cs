// Fofuxo -- glTF scene

using System.IO;
using UnrealBuildTool;

public class FofuxoGltfScene : ModuleRules
{
	public FofuxoGltfScene(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// GLTFMemoryArchive.h lives in GLTFExporter/Private. It is the type the
		// builder's GetBufferData returns, and without the header there is no
		// way to read the buffer's size or its pointer. Same route FofuxoExporter
		// uses to reach FbxExporter.h.
		PrivateIncludePaths.Add(Path.Combine(GetModuleDirectory("GLTFExporter"), "Private"));

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"UnrealEd",
			// The hook.
			"FofuxoExporter",
			// The engine's glTF exporter. Unlike USD, this plugin ships enabled
			// by default -- there is nothing for the user to turn on.
			"GLTFExporter",
		});
	}
}
