// Fofuxo

using System.IO;
using UnrealBuildTool;

public class FofuxoRetargetProps : ModuleRules
{
	public FofuxoRetargetProps(ReadOnlyTargetRules Target) : base(Target)
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
			Path.Combine(ModuleDirectory, "Private", "Attachments"),
			Path.Combine(ModuleDirectory, "Private", "Pose"),
			Path.Combine(ModuleDirectory, "Private", "Viewport"),
			Path.Combine(ModuleDirectory, "Private", "Export"),
		});

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"InputCore",
			"UnrealEd",
			"ToolMenus",
			"EditorFramework",
			"IKRig",
			"AssetRegistry",
			// The op that stores the attachments inside the retargeter. It is a
			// runtime module so the struct type exists in a packaged game; the
			// one that reads the list and hangs the components in the viewport
			// is this module here.
			"FofuxoRetargetOps",
			// For UIKRetargetBatchOperation::RunBatchRetarget, which is the same
			// operation as the Export Selected Animations button.
			"IKRigEditor",
			// None of these is used directly: they are the modules the include
			// chain of IKRetargetEditorController.h passes through.
			"Persona",
			"PropertyEditor",
			"ContentBrowser",
			"EditorWidgets",
			"ToolWidgets",
			"AnimationCore",
			"AnimGraph",
			"AdvancedPreviewScene",
			// For SAdvancedTransformInputBox, the transform widget of the details
			// panel -- the same one IKRigEditor uses on the bone sheet.
			"AnimationWidgets",
		});

		// What we use from the retarget editor is almost all inline
		// (FIKRetargetEditor::GetController) or a public data member (the two
		// preview components) -- for that the include path alone would do. What
		// forces an actual link is RunBatchRetarget, the only IKRIGEDITOR_API
		// symbol we call.
	}
}
