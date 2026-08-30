// Fofuxo

using System.IO;
using UnrealBuildTool;

public class FofuxoRetargetProps : ModuleRules
{
	public FofuxoRetargetProps(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// O nome do plugin mora em Source/FofuxoComum/FofuxoNome.h, que nao e
		// modulo -- e uma pasta de include, para nenhum modulo passar a depender
		// de outro so por causa de uma string.
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "FofuxoComum"));

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
			// O op que guarda os anexos dentro do retargeter. Ele e de runtime
			// para o tipo do struct existir no jogo empacotado; quem le a lista e
			// pendura os componentes no visor e este modulo aqui.
			"FofuxoRetargetOps",
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
			// Pelo SAdvancedTransformInputBox, que e o widget de transform do painel de
			// detalhes -- o mesmo que a IKRigEditor usa na ficha do osso.
			"AnimationWidgets",
			// Pelo FDynamicColoredMaterialRenderProxy das varetas: ele e um
			// FRenderResource, e a vtable dele vem daqui.
			"RenderCore",
		});

		// Do editor de retarget, o que se usa daqui e quase todo inline
		// (FIKRetargetEditor::GetController) ou membro de dado publico (os dois
		// componentes de preview) -- para isso bastaria o caminho de include. O
		// que obriga a linkar de verdade e o RunBatchRetarget, que e o unico
		// simbolo com IKRIGEDITOR_API que a gente chama.
	}
}
