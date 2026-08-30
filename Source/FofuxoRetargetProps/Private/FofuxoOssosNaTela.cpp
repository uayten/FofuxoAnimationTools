// Fofuxo -- desenhar e acertar osso no visor do retarget

#include "FofuxoOssosNaTela.h"

#include "Animation/DebugSkelMeshComponent.h"
#include "DynamicMeshBuilder.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "HitProxies.h"
#include "Materials/Material.h"
#include "EditorModes.h"
#include "Materials/MaterialRenderProxy.h"
#include "Misc/ConfigCacheIni.h"
#include "Preferences/PersonaOptions.h"
#include "PrimitiveDrawingUtils.h"
#include "ReferenceSkeleton.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"
#include "SceneManagement.h"
#include "SceneView.h"
#include "UnrealClient.h"

namespace FofuxoOssos
{
	static const TCHAR* SecaoIni = TEXT("FofuxoRetargetProps");
	static const TCHAR* ChaveIni = TEXT("OssosEmVareta");

	static bool bLidoDoIni = false;
	static bool bLigado = false;

	/** O BoneDrawSize de antes, por retargeter em que mexemos. */
	static TMap<TWeakObjectPtr<UIKRetargeter>, float> Guardado;

	/**
	 * O tamanho a que o desenho da engine encolhe.
	 *
	 * Nao e zero: o hit proxy do osso e o desenho, e um osso de tamanho zero nao
	 * seria clicavel nem pela busca por proximidade, que le o mesmo buffer.
	 */
	static constexpr float TamanhoEncolhido = 0.15f;

	/**
	 * O raio de busca do clique, em pixels.
	 *
	 * Generoso de proposito, porque o mais perto e quem ganha: um raio grande nao
	 * pega o osso errado, so deixa de exigir pontaria. O que ele custa e clicar no
	 * vazio a menos de 22 pixels de um osso ainda selecionar aquele osso.
	 */
	static constexpr int32 RaioEmPixels = 22;

	/** O nome do tipo de proxy que a IKRigEditor poe em cada osso. */
	static const TCHAR* NomeDoProxy = TEXT("HIKRetargetEditorBoneProxy");

	/** A esfera da junta e o cilindro da vareta, em pixels de raio. */
	static constexpr float RaioDaJunta = 3.0f;
	static constexpr float RaioDaVareta = 1.4f;

	/** Quantos lados. Nesse tamanho na tela, mais que isto ninguem distingue. */
	static constexpr int32 LadosDaEsfera = 10;
	static constexpr int32 AneisDaEsfera = 6;
	static constexpr int32 LadosDaVareta = 8;

	/** Um boneco pronto para desenhar. */
	struct FBoneco
	{
		USkeletalMesh* Malha = nullptr;
		TArray<FVector> Onde;

		bool Serve() const { return Malha != nullptr && Onde.Num() > 0; }
		const FReferenceSkeleton& Esqueleto() const { return Malha->GetRefSkeleton(); }
	};

	static void Juntar(const FIKRetargetEditorController& Quem, const ERetargetSourceOrTarget Lado, FBoneco& Out)
	{
		UDebugSkelMeshComponent* Componente = Lado == ERetargetSourceOrTarget::Source
			? Quem.SourceSkelMeshComponent
			: Quem.TargetSkelMeshComponent;

		if (Componente == nullptr)
		{
			return;
		}

		Out.Malha = Componente->GetSkeletalMeshAsset();
		if (Out.Malha == nullptr)
		{
			return;
		}

		const int32 Quantos = Out.Malha->GetRefSkeleton().GetNum();
		Out.Onde.Reserve(Quantos);

		for (int32 Indice = 0; Indice < Quantos; ++Indice)
		{
			Out.Onde.Add(Componente->GetBoneTransform(Indice).GetLocation());
		}
	}

	/**
	 * Quantas unidades de mundo cabem num pixel, naquela profundidade.
	 *
	 * Vale para perspectiva e para ortografica: na ortografica o W sai 1 e o
	 * M[0][0] ja carrega a largura do enquadramento, entao a mesma conta serve para
	 * os dois casos.
	 */
	static float MundoPorPixel(const FSceneView& View, const FVector& Onde)
	{
		const float W = static_cast<float>(View.WorldToScreen(Onde).W);
		const float Foco = static_cast<float>(View.ViewMatrices.GetViewToClip().M[0][0]);
		const float Largura = static_cast<float>(FMath::Max(View.UnscaledViewRect.Width(), 1));

		return FMath::Abs(W) * 2.f / FMath::Max(Foco * Largura, UE_KINDA_SMALL_NUMBER);
	}

	/** Um vertice qualquer da malha. A cor vem do material, entao aqui e branco. */
	static int32 PorVertice(FDynamicMeshBuilder& Malha, const FVector& Onde, const FVector& Normal)
	{
		return Malha.AddVertex(FDynamicMeshVertex(
			FVector3f(Onde),
			FVector3f(FVector::CrossProduct(Normal, FVector::UpVector).GetSafeNormal()),
			FVector3f(Normal),
			FVector2f::ZeroVector,
			FColor::White));
	}

	/** Uma esfera na junta. */
	static void PorEsfera(FDynamicMeshBuilder& Malha, const FVector& Centro, const float Raio)
	{
		int32 Base = INDEX_NONE;

		for (int32 Anel = 0; Anel <= AneisDaEsfera; ++Anel)
		{
			const float Phi = UE_PI * static_cast<float>(Anel) / static_cast<float>(AneisDaEsfera);
			const float SenoPhi = FMath::Sin(Phi);
			const float CossenoPhi = FMath::Cos(Phi);

			for (int32 Lado = 0; Lado <= LadosDaEsfera; ++Lado)
			{
				const float Teta = 2.f * UE_PI * static_cast<float>(Lado) / static_cast<float>(LadosDaEsfera);

				const FVector Normal(SenoPhi * FMath::Cos(Teta), SenoPhi * FMath::Sin(Teta), CossenoPhi);

				const int32 Qual = PorVertice(Malha, Centro + Normal * Raio, Normal);
				if (Base == INDEX_NONE)
				{
					Base = Qual;
				}
			}
		}

		const int32 PorAnel = LadosDaEsfera + 1;

		for (int32 Anel = 0; Anel < AneisDaEsfera; ++Anel)
		{
			for (int32 Lado = 0; Lado < LadosDaEsfera; ++Lado)
			{
				const int32 Aqui = Base + Anel * PorAnel + Lado;
				const int32 Abaixo = Aqui + PorAnel;

				Malha.AddTriangle(Aqui, Abaixo, Aqui + 1);
				Malha.AddTriangle(Aqui + 1, Abaixo, Abaixo + 1);
			}
		}
	}

	/** Um cilindro entre duas juntas, com um raio proprio em cada ponta. */
	static void PorCilindro(
		FDynamicMeshBuilder& Malha,
		const FVector& De, const float RaioDe,
		const FVector& Ate, const float RaioAte)
	{
		const FVector Eixo = Ate - De;
		const double Comprimento = Eixo.Size();

		if (Comprimento <= UE_KINDA_SMALL_NUMBER)
		{
			return;
		}

		const FVector Z = Eixo / Comprimento;

		FVector X, Y;
		Z.FindBestAxisVectors(X, Y);

		int32 Base = INDEX_NONE;

		for (int32 Lado = 0; Lado < LadosDaVareta; ++Lado)
		{
			const float Angulo = 2.f * UE_PI * static_cast<float>(Lado) / static_cast<float>(LadosDaVareta);
			const FVector Normal = X * FMath::Cos(Angulo) + Y * FMath::Sin(Angulo);

			const int32 Qual = PorVertice(Malha, De + Normal * RaioDe, Normal);
			if (Base == INDEX_NONE)
			{
				Base = Qual;
			}

			PorVertice(Malha, Ate + Normal * RaioAte, Normal);
		}

		for (int32 Lado = 0; Lado < LadosDaVareta; ++Lado)
		{
			const int32 Aqui = Base + Lado * 2;
			const int32 Proximo = Base + ((Lado + 1) % LadosDaVareta) * 2;

			Malha.AddTriangle(Aqui, Aqui + 1, Proximo + 1);
			Malha.AddTriangle(Aqui, Proximo + 1, Proximo);
		}
	}

	/**
	 * Manda a malha para a tela.
	 *
	 * O InvisibleHitProxyId nao e detalhe: sem ele esta geometria entraria no buffer
	 * de hit proxy sem identidade nenhuma e taparia os ossos da engine, que sao
	 * justamente o que a busca por proximidade procura -- o desenho novo apagaria a
	 * selecao que ele deveria facilitar.
	 */
	static void Soltar(
		FDynamicMeshBuilder& Malha,
		FPrimitiveDrawInterface* PDI,
		UMaterial* Material,
		const FLinearColor& Cor)
	{
		// A versao "Dynamic" e que e FDynamicPrimitiveResource, e por isso a unica
		// que o PDI aceita adotar. A outra, a FColoredMaterialRenderProxy pelada, e
		// para o FMeshElementCollector, que tem o registro dele proprio.
		FDynamicColoredMaterialRenderProxy* Pintado =
			new FDynamicColoredMaterialRenderProxy(Material->GetRenderProxy(), Cor);

		PDI->RegisterDynamicResource(Pintado);

		// SDPG_Foreground: o osso atravessa a malha. Sem isso a mao esconde os dedos,
		// que e justamente onde este desenho serve para alguma coisa.
		Malha.Draw(
			PDI,
			FMatrix::Identity,
			Pintado,
			SDPG_Foreground,
			/*bDisableBackfaceCulling*/ false,
			/*bReceivesDecals*/ false,
			FHitProxyId::InvisibleHitProxyId);
	}
}

bool FFofuxoOssosNaTela::EstaLigado()
{
	if (!FofuxoOssos::bLidoDoIni)
	{
		FofuxoOssos::bLidoDoIni = true;
		GConfig->GetBool(FofuxoOssos::SecaoIni, FofuxoOssos::ChaveIni,
			FofuxoOssos::bLigado, GEditorPerProjectIni);
	}

	return FofuxoOssos::bLigado;
}

void FFofuxoOssosNaTela::Alternar()
{
	// A leitura preguicosa antes: sem isto a primeira consulta depois desta iria ao
	// ini e desfaria a escolha.
	EstaLigado();

	FofuxoOssos::bLigado = !FofuxoOssos::bLigado;

	GConfig->SetBool(FofuxoOssos::SecaoIni, FofuxoOssos::ChaveIni,
		FofuxoOssos::bLigado, GEditorPerProjectIni);
}

void FFofuxoOssosNaTela::Acompanhar(FIKRetargetEditor& Editor)
{
	const TSharedRef<FIKRetargetEditorController> Quem = Editor.GetController();

	UIKRetargeter* Asset = Quem->AssetController != nullptr
		? Quem->AssetController->GetAsset()
		: nullptr;

	if (Asset == nullptr)
	{
		return;
	}

	if (EstaLigado())
	{
		if (!FofuxoOssos::Guardado.Contains(Asset))
		{
			FofuxoOssos::Guardado.Add(Asset, Asset->BoneDrawSize);
		}

		// Reposto a cada passeio, e nao so na virada: reabrir o asset ou um Ctrl+Z
		// devolvem o valor do disco, e o desenho da engine voltaria a engrossar por
		// baixo das varetas.
		Asset->BoneDrawSize = FofuxoOssos::TamanhoEncolhido;

		return;
	}

	if (const float* Antes = FofuxoOssos::Guardado.Find(Asset))
	{
		Asset->BoneDrawSize = *Antes;
		FofuxoOssos::Guardado.Remove(Asset);
	}
}

void FFofuxoOssosNaTela::Esquecer()
{
	for (const TTuple<TWeakObjectPtr<UIKRetargeter>, float>& Par : FofuxoOssos::Guardado)
	{
		if (UIKRetargeter* Asset = Par.Key.Get())
		{
			Asset->BoneDrawSize = Par.Value;
		}
	}

	FofuxoOssos::Guardado.Reset();
}

void FFofuxoOssosNaTela::Desenhar(
	const FIKRetargetEditorController& Quem,
	const FSceneView* View,
	FPrimitiveDrawInterface* PDI)
{
	if (!EstaLigado() || View == nullptr || PDI == nullptr)
	{
		return;
	}

	const UIKRetargeter* Asset = Quem.AssetController != nullptr
		? Quem.AssetController->GetAsset()
		: nullptr;

	UMaterial* Material = GEngine != nullptr ? GEngine->ShadedLevelColorationUnlitMaterial : nullptr;

	if (Asset == nullptr || Material == nullptr)
	{
		return;
	}

	const UPersonaOptions* Opcoes = GetDefault<UPersonaOptions>();
	const ERetargetSourceOrTarget Editavel = Quem.GetSourceOrTarget();
	const TArray<FName>& Selecionados = Quem.GetSelectedBones();

	for (const ERetargetSourceOrTarget Lado : {ERetargetSourceOrTarget::Source, ERetargetSourceOrTarget::Target})
	{
		// A mesma caixa que esconde o esqueleto da engine esconde a vareta: sao o
		// mesmo osso desenhado de duas maneiras.
		const bool bAVista = Lado == ERetargetSourceOrTarget::Source
			? Asset->bShowSourceSkeleton
			: Asset->bShowTargetSkeleton;

		if (!bAVista)
		{
			continue;
		}

		FofuxoOssos::FBoneco Boneco;
		FofuxoOssos::Juntar(Quem, Lado, Boneco);

		if (!Boneco.Serve())
		{
			continue;
		}

		const bool bEditavel = Lado == Editavel;

		// Duas malhas, e nao uma por osso: o material da cor e um so para o desenho
		// inteiro, entao os ossos de cada cor vao juntos. Sao no maximo quatro
		// desenhos na tela toda -- com um por osso seriam uns quinhentos.
		FDynamicMeshBuilder Normais(View->GetFeatureLevel());
		FDynamicMeshBuilder Escolhidos(View->GetFeatureLevel());

		bool bTemNormais = false;
		bool bTemEscolhidos = false;

		const FReferenceSkeleton& Esqueleto = Boneco.Esqueleto();

		for (int32 Indice = 0; Indice < Boneco.Onde.Num(); ++Indice)
		{
			const FVector& Onde = Boneco.Onde[Indice];

			const bool bEscolhido = bEditavel && Selecionados.Contains(Esqueleto.GetBoneName(Indice));

			FDynamicMeshBuilder& Malha = bEscolhido ? Escolhidos : Normais;
			bool& bTem = bEscolhido ? bTemEscolhidos : bTemNormais;
			bTem = true;

			// Tamanho constante na tela: e o que diferencia isto do desenho da engine,
			// que e medido em unidades de mundo -- la o mesmo osso e uma bola no pulso
			// e some quando a camera afasta.
			const float PorPixel = FofuxoOssos::MundoPorPixel(*View, Onde);

			FofuxoOssos::PorEsfera(Malha, Onde, PorPixel * FofuxoOssos::RaioDaJunta);

			const int32 Pai = Esqueleto.GetParentIndex(Indice);
			if (!Boneco.Onde.IsValidIndex(Pai))
			{
				continue;
			}

			// A vareta pertence ao pai, como no Blender e como na engine: ela sai da
			// junta do osso e vai ate a do filho, e clicar nela seleciona o pai. Por
			// isso a cor dela e a do pai, e nao a deste osso.
			const bool bPaiEscolhido = bEditavel && Selecionados.Contains(Esqueleto.GetBoneName(Pai));

			FDynamicMeshBuilder& MalhaDaVareta = bPaiEscolhido ? Escolhidos : Normais;
			bool& bTemVareta = bPaiEscolhido ? bTemEscolhidos : bTemNormais;
			bTemVareta = true;

			const FVector& NoPai = Boneco.Onde[Pai];

			// Um raio em cada ponta, e nao um so: numa coluna vista de esguelha as
			// duas pontas estao a distancias bem diferentes da camera, e um raio unico
			// engrossaria a ponta de tras.
			FofuxoOssos::PorCilindro(
				MalhaDaVareta,
				NoPai, FofuxoOssos::MundoPorPixel(*View, NoPai) * FofuxoOssos::RaioDaVareta,
				Onde, PorPixel * FofuxoOssos::RaioDaVareta);
		}

		const FLinearColor Normal = bEditavel ? Opcoes->DefaultBoneColor : Opcoes->DisabledBoneColor;

		if (bTemNormais)
		{
			FofuxoOssos::Soltar(Normais, PDI, Material, Normal);
		}

		if (bTemEscolhidos)
		{
			FofuxoOssos::Soltar(Escolhidos, PDI, Material, Opcoes->SelectedBoneColor);
		}
	}
}

bool FFofuxoOssosNaTela::EhDeOsso(HHitProxy* Proxy)
{
	// Pelo nome do tipo, e nao por IsA: o HIKRetargetEditorBoneProxy::StaticGetType
	// e da IKRigEditor e nao e exportado, entao nao ha o que comparar de fora. O
	// nome esta no proprio HHitProxyType, posto pelo IMPLEMENT_HIT_PROXY.
	return Proxy != nullptr
		&& Proxy->GetType() != nullptr
		&& FCString::Strcmp(Proxy->GetType()->GetName(), FofuxoOssos::NomeDoProxy) == 0;
}

HHitProxy* FFofuxoOssosNaTela::OssoPertoDoCursor(FViewport& Visor, const int32 X, const int32 Y)
{
	const FIntPoint Tamanho = Visor.GetSizeXY();
	if (Tamanho.X <= 0 || Tamanho.Y <= 0)
	{
		return nullptr;
	}

	const FIntRect Caixa(
		FMath::Max(X - FofuxoOssos::RaioEmPixels, 0),
		FMath::Max(Y - FofuxoOssos::RaioEmPixels, 0),
		FMath::Min(X + FofuxoOssos::RaioEmPixels + 1, Tamanho.X),
		FMath::Min(Y + FofuxoOssos::RaioEmPixels + 1, Tamanho.Y));

	if (Caixa.Width() <= 0 || Caixa.Height() <= 0)
	{
		return nullptr;
	}

	TArray<HHitProxy*> Mapa;
	Visor.GetHitProxyMap(Caixa, Mapa);

	if (Mapa.Num() < Caixa.Width() * Caixa.Height())
	{
		return nullptr;
	}

	HHitProxy* Melhor = nullptr;
	int32 MenorDistancia = MAX_int32;

	for (int32 Linha = 0; Linha < Caixa.Height(); ++Linha)
	{
		for (int32 Coluna = 0; Coluna < Caixa.Width(); ++Coluna)
		{
			HHitProxy* Proxy = Mapa[Linha * Caixa.Width() + Coluna];
			if (!EhDeOsso(Proxy))
			{
				continue;
			}

			const int32 DeLado = Caixa.Min.X + Coluna - X;
			const int32 DeCima = Caixa.Min.Y + Linha - Y;
			const int32 Distancia = DeLado * DeLado + DeCima * DeCima;

			if (Distancia < MenorDistancia)
			{
				MenorDistancia = Distancia;
				Melhor = Proxy;
			}
		}
	}

	return Melhor;
}
