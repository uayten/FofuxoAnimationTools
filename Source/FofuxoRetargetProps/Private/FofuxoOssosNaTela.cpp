// Fofuxo -- desenhar e acertar osso no visor do retarget

#include "FofuxoOssosNaTela.h"

#include "Animation/DebugSkelMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "HitProxies.h"
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

	/** O circulo da junta e a espessura da vareta, em pixels. */
	static constexpr float RaioDaJunta = 3.2f;
	static constexpr float EspessuraDaVareta = 1.6f;

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

	if (Asset == nullptr)
	{
		return;
	}

	const UPersonaOptions* Opcoes = GetDefault<UPersonaOptions>();
	const ERetargetSourceOrTarget Editavel = Quem.GetSourceOrTarget();
	const TArray<FName>& Selecionados = Quem.GetSelectedBones();

	const FVector Direita = View->GetViewRight();
	const FVector Cima = View->GetViewUp();

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

		// O lado que nao se edita sai apagado, como na engine: ele e referencia, e
		// nao ha o que clicar nele.
		const FLinearColor Normal = bEditavel ? Opcoes->DefaultBoneColor : Opcoes->DisabledBoneColor;

		const FReferenceSkeleton& Esqueleto = Boneco.Esqueleto();

		for (int32 Indice = 0; Indice < Boneco.Onde.Num(); ++Indice)
		{
			const FVector& Onde = Boneco.Onde[Indice];

			const bool bSelecionado = bEditavel && Selecionados.Contains(Esqueleto.GetBoneName(Indice));
			const FLinearColor Cor = bSelecionado ? Opcoes->SelectedBoneColor : Normal;

			// Tamanho constante na tela: e o que diferencia isto do desenho da
			// engine, que e medido em unidades de mundo e por isso some ao afastar.
			const float PorPixel = FofuxoOssos::MundoPorPixel(*View, Onde);

			// SDPG_Foreground: o osso atravessa a malha. Sem isso a mao esconde os
			// dedos, que e justamente onde este desenho serve para alguma coisa.
			DrawCircle(
				PDI, Onde, Direita, Cima, Cor,
				PorPixel * FofuxoOssos::RaioDaJunta,
				/*NumSides*/ 12,
				SDPG_Foreground,
				PorPixel * FofuxoOssos::EspessuraDaVareta);

			const int32 Pai = Esqueleto.GetParentIndex(Indice);
			if (Boneco.Onde.IsValidIndex(Pai))
			{
				// A vareta pertence ao pai, como no Blender e como na engine: a linha
				// sai da junta do osso e vai ate a do filho, e clicar nela seleciona o
				// pai.
				const FLinearColor CorDaLinha =
					(bEditavel && Selecionados.Contains(Esqueleto.GetBoneName(Pai)))
						? Opcoes->SelectedBoneColor
						: Normal;

				PDI->DrawLine(
					Boneco.Onde[Pai], Onde, CorDaLinha, SDPG_Foreground,
					PorPixel * FofuxoOssos::EspessuraDaVareta);
			}
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
