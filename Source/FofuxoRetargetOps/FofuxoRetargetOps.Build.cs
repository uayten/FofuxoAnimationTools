// Fofuxo

using UnrealBuildTool;

public class FofuxoRetargetOps : ModuleRules
{
	public FofuxoRetargetOps(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Modulo de runtime, e nao de editor, por um motivo so: o op e um
		// FInstancedStruct salvo dentro do UIKRetargeter, que e um asset de
		// runtime. Se o tipo do struct so existisse no editor, o asset cozinhado
		// carregaria sem conseguir resolver o tipo e a lista sumiria com um aviso.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"IKRig",
		});
	}
}
