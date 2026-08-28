// Fofuxo's Exporter

using UnrealBuildTool;

public class FofuxoRetargetProps : ModuleRules
{
	public FofuxoRetargetProps(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

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
			// Pelo UIKRetargetBatchOperation::RunBatchRetarget, que e a mesma
			// operacao do botao Export Selected Animations.
			"IKRigEditor",
			// Nada disto e usado direto: sao os modulos por onde passa a cadeia
			// de include do IKRetargetEditorController.h.
			"Persona",
			"PropertyEditor",
			"ContentBrowser",
			"EditorWidgets",
			"ToolWidgets",
			"AnimationCore",
			"AnimGraph",
			"AdvancedPreviewScene",
		});

		// Do editor de retarget, o que se usa daqui e quase todo inline
		// (FIKRetargetEditor::GetController) ou membro de dado publico (os dois
		// componentes de preview) -- para isso bastaria o caminho de include. O
		// que obriga a linkar de verdade e o RunBatchRetarget, que e o unico
		// simbolo com IKRIGEDITOR_API que a gente chama.
	}
}
