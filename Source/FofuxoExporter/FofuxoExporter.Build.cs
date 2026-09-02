// Fofuxo

using System.IO;
using UnrealBuildTool;

public class FofuxoExporter : ModuleRules
{
	public FofuxoExporter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The plugin's name lives in Source/FofuxoCommon/FofuxoName.h, which is
		// not a module -- it is an include folder, so that no module ends up
		// depending on another one just for a string.
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "FofuxoCommon"));

		// Private/ is split by subject, and UBT only puts its root on the include
		// path. Listing the subfolders here is what keeps every include a bare file
		// name -- moving a file between subfolders then touches no other file.
		PrivateIncludePaths.AddRange(new string[]
		{
			Path.Combine(ModuleDirectory, "Private", "Export"),
			Path.Combine(ModuleDirectory, "Private", "Writers"),
			Path.Combine(ModuleDirectory, "Private", "UI"),
		});

		// FbxExporter.h lives in UnrealEd/Private. Its methods are UNREALED_API,
		// so they link from outside; it is only the header that is not public.
		// Same route the engine's own MovieSceneTools uses to reach the exporter.
		PrivateIncludePaths.Add(Path.Combine(GetModuleDirectory("UnrealEd"), "Private"));

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"InputCore",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"ToolMenus",
			"ContentBrowser",
			"AssetTools",
			"AssetRegistry",
			"PropertyEditor",
			"EditorFramework",
			"ToolWidgets",
			"DesktopPlatform",
			// FbxExporter.h pulls in Sequencer headers for the animation track
			// adapter's type, even though we use none of it.
			"MovieScene",
			"MovieSceneTracks",
			"LevelSequence",
		});

		AddEngineThirdPartyPrivateStaticDependencies(Target, "FBX");
	}
}
