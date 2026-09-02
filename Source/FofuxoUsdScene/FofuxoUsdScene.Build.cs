// Fofuxo -- USD scene

using UnrealBuildTool;

public class FofuxoUsdScene : ModuleRules
{
	public FofuxoUsdScene(ReadOnlyTargetRules Target) : base(Target)
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
			// The hook: this is where the exporter calls scene writing from.
			"FofuxoExporter",
			// USDClasses brings EUsdUpAxis; USDUtilities, the skeleton and
			// animation conversions; UnrealUSDWrapper, the stage and the prims.
			"USDClasses",
			"USDUtilities",
			"UnrealUSDWrapper",
		});

		// Without this there is no USE_USD_SDK and no Pixar headers, and the
		// signatures taking pxr::UsdPrim stay out of reach. UnrealUSDWrapper
		// itself documents this helper as the way for an outside module to
		// consume the SDK.
		// Qualified: the wrapper's build class lives in UnrealBuildTool.Rules,
		// and the engine's own Build.cs files call it unprefixed because they
		// share the namespace. This one doesn't.
		UnrealBuildTool.Rules.UnrealUSDWrapper.CheckAndSetupUsdSdk(Target, this);
	}
}
