// Fofuxo

using System.IO;
using UnrealBuildTool;

public class FofuxoExporter : ModuleRules
{
	public FofuxoExporter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// O nome do plugin mora em Source/FofuxoComum/FofuxoNome.h, que nao e
		// modulo -- e uma pasta de include, para nenhum modulo passar a depender
		// de outro so por causa de uma string.
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "..", "FofuxoComum"));

		// FbxExporter.h mora em UnrealEd/Private. Os metodos dele sao UNREALED_API,
		// entao linkam de fora; so o cabecalho e que nao e publico. Mesmo caminho
		// que o MovieSceneTools da engine usa para chegar no exportador.
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
			// FbxExporter.h puxa cabecalhos de Sequencer para o tipo do adaptador
			// de trilha de animacao, mesmo sem a gente usar nada disso.
			"MovieScene",
			"MovieSceneTracks",
			"LevelSequence",
		});

		AddEngineThirdPartyPrivateStaticDependencies(Target, "FBX");
	}
}
