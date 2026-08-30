// Fofuxo -- cena glTF

using UnrealBuildTool;

public class FofuxoGltfCena : ModuleRules
{
	public FofuxoGltfCena(ReadOnlyTargetRules Target) : base(Target)
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
			"UnrealEd",
			// O gancho.
			"FofuxoExporter",
			// O exportador glTF da engine. Diferente do USD, este plugin ja vem
			// ligado por padrao -- nao ha nada para o usuario habilitar.
			"GLTFExporter",
		});
	}
}
