// Fofuxo

using UnrealBuildTool;

public class FofuxoRetargetOps : ModuleRules
{
	public FofuxoRetargetOps(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// A runtime module rather than an editor one, for a single reason: the op
		// is an FInstancedStruct saved inside the UIKRetargeter, which is a
		// runtime asset. If the struct type only existed in the editor, the
		// cooked asset would load unable to resolve the type and the list would
		// vanish with a warning.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"IKRig",
		});
	}
}
