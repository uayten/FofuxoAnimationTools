// Fofuxo's Exporter -- cena USD

using UnrealBuildTool;

public class FofuxoUsdCena : ModuleRules
{
	public FofuxoUsdCena(ReadOnlyTargetRules Target) : base(Target)
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
			// O gancho: e daqui que o exportador chama a escrita de cena.
			"FofuxoExporter",
			// USDClasses traz o EUsdUpAxis; USDUtilities, as conversoes de
			// esqueleto e de animacao; UnrealUSDWrapper, o stage e os prims.
			"USDClasses",
			"USDUtilities",
			"UnrealUSDWrapper",
		});

		// Sem isto nao ha USE_USD_SDK nem os cabecalhos da Pixar, e as assinaturas
		// que recebem pxr::UsdPrim ficam inalcancaveis. O proprio UnrealUSDWrapper
		// documenta este helper como o jeito certo de um modulo de fora consumir
		// o SDK.
		// Qualificado: a classe de build do wrapper vive em UnrealBuildTool.Rules,
		// e os Build.cs da engine a chamam sem prefixo por estarem no mesmo
		// namespace. Este aqui nao esta.
		UnrealBuildTool.Rules.UnrealUSDWrapper.CheckAndSetupUsdSdk(Target, this);
	}
}
