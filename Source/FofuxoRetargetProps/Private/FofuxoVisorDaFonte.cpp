// Fofuxo -- uma segunda janela de visor, presa no osso da fonte

#include "FofuxoVisorDaFonte.h"

#include "Algo/Reverse.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "EditorViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Docking/WorkspaceItem.h"
#include "IPersonaPreviewScene.h"
#include "IPersonaToolkit.h"
#include "PreviewScene.h"
#include "ReferenceSkeleton.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargetChainMapping.h"
#include "Retargeter/IKRetargetOps.h"
#include "Retargeter/IKRetargeter.h"
#include "Rig/IKRigDefinition.h"
#include "SEditorViewport.h"
#include "StructUtils/InstancedStruct.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

const FName FFofuxoVisorDaFonte::IdDaAba("FofuxoVisorDaFonte");

namespace FofuxoVisorDaFonte
{
	/** Os gerentes de aba onde ja pusemos o registro, para tirar no shutdown. */
	static TArray<TWeakPtr<FTabManager>> Registrados;

	/**
	 * Os ossos de uma cadeia, do inicio ate o fim.
	 *
	 * A cadeia e definida por dois nomes, e o caminho entre eles so existe de baixo
	 * para cima: um osso tem um pai, e nao uma lista de filhos. Entao sobe-se do fim
	 * ate encontrar o inicio, e o resultado e virado.
	 *
	 * Devolve false se o fim nao descende do inicio -- cadeia mal definida, ou nomes
	 * que nao sao deste esqueleto.
	 */
	static bool OssosDaCadeia(
		const FReferenceSkeleton& Esqueleto,
		const FName Inicio,
		const FName Fim,
		TArray<FName>& OutOssos)
	{
		const int32 IndiceInicio = Esqueleto.FindBoneIndex(Inicio);
		const int32 IndiceFim = Esqueleto.FindBoneIndex(Fim);

		if (IndiceInicio == INDEX_NONE || IndiceFim == INDEX_NONE)
		{
			return false;
		}

		TArray<FName> DeTrasParaFrente;

		int32 Andando = IndiceFim;
		while (Andando != INDEX_NONE)
		{
			DeTrasParaFrente.Add(Esqueleto.GetBoneName(Andando));

			if (Andando == IndiceInicio)
			{
				break;
			}

			Andando = Esqueleto.GetParentIndex(Andando);
		}

		if (Andando != IndiceInicio)
		{
			return false;
		}

		Algo::Reverse(DeTrasParaFrente);
		OutOssos = MoveTemp(DeTrasParaFrente);

		return true;
	}

	/** O mapeamento de cadeias deste retargeter, que mora no primeiro op que tem um. */
	static const FRetargetChainMapping* MapeamentoDe(const UIKRetargeter* Retargeter)
	{
		if (Retargeter == nullptr)
		{
			return nullptr;
		}

		for (const FInstancedStruct& Op : Retargeter->GetRetargetOps())
		{
			if (const FIKRetargetOpBase* Base = Op.GetPtr<FIKRetargetOpBase>())
			{
				if (const FRetargetChainMapping* Mapeamento = Base->GetChainMapping())
				{
					return Mapeamento;
				}
			}
		}

		return nullptr;
	}

	/**
	 * O osso da fonte que corresponde a um osso do alvo.
	 *
	 * Pelo mapeamento de cadeias, que e a unica correspondencia que o retargeter de
	 * fato conhece: achada a cadeia do alvo que contem o osso, o correspondente e o
	 * osso na mesma posicao proporcional da cadeia mapeada. Uma mao de tres ossos
	 * mapeada numa de quatro nao tem resposta exata, e a proporcao e a resposta
	 * menos errada.
	 *
	 * Sem cadeia que sirva, tenta o mesmo nome na fonte -- que funciona entre
	 * esqueletos da convencao da Unreal -- e depois o pelvis, que pelo menos poe a
	 * camera no boneco certo.
	 */
	static FName CorrespondenteNaFonte(
		const FIKRetargetEditorController& Quem,
		const FName OssoDoAlvo,
		const FReferenceSkeleton& EsqueletoDaFonte)
	{
		UIKRetargeterController* Controle = Quem.AssetController;
		if (Controle == nullptr || OssoDoAlvo.IsNone())
		{
			return NAME_None;
		}

		// Se voce esta editando a fonte, o osso selecionado ja e da fonte.
		if (Quem.GetSourceOrTarget() == ERetargetSourceOrTarget::Source)
		{
			return OssoDoAlvo;
		}

		const UDebugSkelMeshComponent* Alvo = Quem.TargetSkelMeshComponent;
		const USkeletalMesh* MalhaAlvo = Alvo != nullptr ? Alvo->GetSkeletalMeshAsset() : nullptr;

		const UIKRigDefinition* RigAlvo = Controle->GetIKRig(ERetargetSourceOrTarget::Target);
		const UIKRigDefinition* RigFonte = Controle->GetIKRig(ERetargetSourceOrTarget::Source);
		const FRetargetChainMapping* Mapeamento = MapeamentoDe(Controle->GetAsset());

		if (MalhaAlvo != nullptr && RigAlvo != nullptr && RigFonte != nullptr && Mapeamento != nullptr)
		{
			const FReferenceSkeleton& EsqueletoDoAlvo = MalhaAlvo->GetRefSkeleton();

			for (const FBoneChain& Cadeia : RigAlvo->GetRetargetChains())
			{
				TArray<FName> OssosDoAlvo;
				if (!OssosDaCadeia(EsqueletoDoAlvo, Cadeia.StartBone.BoneName, Cadeia.EndBone.BoneName, OssosDoAlvo))
				{
					continue;
				}

				const int32 Onde = OssosDoAlvo.IndexOfByKey(OssoDoAlvo);
				if (Onde == INDEX_NONE)
				{
					continue;
				}

				const FName NomeNaFonte =
					Mapeamento->GetChainMappedTo(Cadeia.ChainName, ERetargetSourceOrTarget::Target);

				if (NomeNaFonte.IsNone())
				{
					break;
				}

				for (const FBoneChain& Outra : RigFonte->GetRetargetChains())
				{
					if (Outra.ChainName != NomeNaFonte)
					{
						continue;
					}

					TArray<FName> OssosDaFonte;
					if (!OssosDaCadeia(EsqueletoDaFonte, Outra.StartBone.BoneName, Outra.EndBone.BoneName, OssosDaFonte)
						|| OssosDaFonte.IsEmpty())
					{
						break;
					}

					if (OssosDoAlvo.Num() <= 1 || OssosDaFonte.Num() <= 1)
					{
						return OssosDaFonte[0];
					}

					const float Fracao = static_cast<float>(Onde) / static_cast<float>(OssosDoAlvo.Num() - 1);
					const int32 Escolhido = FMath::Clamp(
						FMath::RoundToInt(Fracao * static_cast<float>(OssosDaFonte.Num() - 1)),
						0,
						OssosDaFonte.Num() - 1);

					return OssosDaFonte[Escolhido];
				}

				break;
			}
		}

		// Mesmo nome: entre dois esqueletos da convencao da Unreal isto acerta em
		// cheio, e nao custa nada tentar.
		if (EsqueletoDaFonte.FindBoneIndex(OssoDoAlvo) != INDEX_NONE)
		{
			return OssoDoAlvo;
		}

		const FName Pelvis = Controle->GetPelvisBone(ERetargetSourceOrTarget::Source);
		return EsqueletoDaFonte.FindBoneIndex(Pelvis) != INDEX_NONE ? Pelvis : NAME_None;
	}
}

/**
 * A camera deste visor: segue, quadro a quadro, o osso da fonte que corresponde
 * ao osso selecionado no visor principal.
 */
class FFofuxoCameraDaFonte : public FEditorViewportClient
{
public:
	FFofuxoCameraDaFonte(
		FPreviewScene* Cena,
		const TSharedRef<SEditorViewport>& Visor,
		const TWeakPtr<FIKRetargetEditorController>& Quem)
		: FEditorViewportClient(nullptr, Cena, Visor)
		, Controlador(Quem)
	{
		SetViewportType(LVT_Perspective);
		SetViewMode(VMI_Lit);

		// Sem isto a animacao so andaria quando alguma coisa pedisse redesenho, e o
		// visor ficaria congelado num quadro enquanto o principal roda.
		SetRealtime(true);

		// Um visor de olhar: nao ha selecao propria aqui, e o contorno de selecao do
		// visor principal desenhado aqui so confundiria.
		EngineShowFlags.SetSelectionOutline(false);
	}

	virtual void Tick(float DeltaSeconds) override
	{
		FEditorViewportClient::Tick(DeltaSeconds);
		Acompanhar();
	}

private:
	void Acompanhar()
	{
		const TSharedPtr<FIKRetargetEditorController> Quem = Controlador.Pin();
		if (!Quem.IsValid())
		{
			return;
		}

		UDebugSkelMeshComponent* Fonte = Quem->SourceSkelMeshComponent;
		USkeletalMesh* Malha = Fonte != nullptr ? Fonte->GetSkeletalMeshAsset() : nullptr;

		if (Malha == nullptr)
		{
			return;
		}

		const TArray<FName>& Selecionados = Quem->GetSelectedBones();
		const FName Clicado = Selecionados.IsEmpty() ? NAME_None : Selecionados.Last();

		// A conta das cadeias so refaz quando a selecao muda: ela varre as cadeias
		// dos dois rigs, e isso a sessenta quadros por segundo seria desperdicio.
		if (Clicado != UltimoClicado)
		{
			UltimoClicado = Clicado;
			OssoDaFonte = FofuxoVisorDaFonte::CorrespondenteNaFonte(*Quem, Clicado, Malha->GetRefSkeleton());
			bPrecisaEnquadrar = true;
		}

		if (OssoDaFonte.IsNone())
		{
			return;
		}

		const int32 Indice = Malha->GetRefSkeleton().FindBoneIndex(OssoDaFonte);
		if (Indice == INDEX_NONE)
		{
			return;
		}

		const FVector Onde = Fonte->GetBoneTransform(Indice).GetLocation();

		if (bPrecisaEnquadrar && !bJaEnquadrou)
		{
			// So a primeira vez de todas. Dai em diante a distancia e a orbita sao as
			// que voce deixou -- reenquadrar a cada osso desfaria o zoom que voce
			// acabou de dar para ver o dedo.
			bJaEnquadrou = true;
			FocusViewportOnBox(FBox::BuildAABB(Onde, FVector(TamanhoDoOsso(*Fonte, Indice))), true);
		}
		else
		{
			// A distancia sai do estado atual, e nao de uma constante: assim a roda do
			// mouse continua sendo o zoom, e nos so mexemos no centro.
			const double Distancia = FMath::Max((GetViewLocation() - GetLookAtLocation()).Size(), 5.0);
			SetViewLocationForOrbiting(Onde, static_cast<float>(Distancia));
		}

		bPrecisaEnquadrar = false;
	}

	/** Um raio plausivel para enquadrar: o tamanho do proprio osso, com um chao. */
	static float TamanhoDoOsso(const USkeletalMeshComponent& Componente, const int32 Indice)
	{
		const FReferenceSkeleton& Esqueleto = Componente.GetSkeletalMeshAsset()->GetRefSkeleton();
		const int32 Pai = Esqueleto.GetParentIndex(Indice);

		if (Pai == INDEX_NONE)
		{
			return 100.f;
		}

		const float Comprimento = static_cast<float>(FVector::Dist(
			Componente.GetBoneTransform(Indice).GetLocation(),
			Componente.GetBoneTransform(Pai).GetLocation()));

		return FMath::Clamp(Comprimento * 3.f, 5.f, 200.f);
	}

	TWeakPtr<FIKRetargetEditorController> Controlador;

	FName UltimoClicado = NAME_None;
	FName OssoDaFonte = NAME_None;

	bool bPrecisaEnquadrar = false;
	bool bJaEnquadrou = false;
};

/** O widget da aba: um visor pelado sobre a cena de preview do editor. */
class SFofuxoVisorDaFonte : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SFofuxoVisorDaFonte) {}
	SLATE_END_ARGS()

	void Construct(
		const FArguments& InArgs,
		const TWeakPtr<FIKRetargetEditorController>& InControlador,
		const TSharedPtr<IPersonaPreviewScene>& InCena)
	{
		Controlador = InControlador;

		// Guardado como referencia forte de proposito: o cliente do visor fica com um
		// ponteiro cru para a cena, e a ordem de destruicao no fechamento do editor
		// nao e nossa para garantir.
		Cena = InCena;

		SEditorViewport::Construct(SEditorViewport::FArguments());
	}

	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override
	{
		return MakeShared<FFofuxoCameraDaFonte>(
			Cena.IsValid() ? static_cast<FPreviewScene*>(Cena.Get()) : nullptr,
			SharedThis(this),
			Controlador);
	}

private:
	TWeakPtr<FIKRetargetEditorController> Controlador;
	TSharedPtr<IPersonaPreviewScene> Cena;
};

void FFofuxoVisorDaFonte::GarantirAba(FIKRetargetEditor& Editor)
{
	const TSharedPtr<FTabManager> Abas = Editor.GetTabManager();
	if (!Abas.IsValid())
	{
		return;
	}

	// Este passeio acontece duas vezes por segundo.
	if (Abas->HasTabSpawner(IdDaAba))
	{
		return;
	}

	const TWeakPtr<FIKRetargetEditorController> Fraco = Editor.GetController();
	const TSharedPtr<IPersonaPreviewScene> Cena = Editor.GetPersonaToolkit()->GetPreviewScene();

	Abas->RegisterTabSpawner(IdDaAba, FOnSpawnTab::CreateLambda(
		[Fraco, Cena](const FSpawnTabArgs&) -> TSharedRef<SDockTab>
		{
			return SNew(SDockTab)
				.Label(LOCTEXT("VisorDaFonteAba", "Fonte (Fofuxo)"))
				[
					SNew(SFofuxoVisorDaFonte, Fraco, Cena)
				];
		}))
		.SetDisplayName(LOCTEXT("VisorDaFonteAba", "Fonte (Fofuxo)"))
		.SetTooltipText(LOCTEXT("VisorDaFonteAbaTip",
			"Um segundo visor da mesma cena, com a camera colada no osso da fonte que "
			"corresponde ao osso selecionado. Serve para ver o gabarito e o ajuste ao "
			"mesmo tempo, sem viajar de camera entre os dois bonecos."))
		.SetGroup(Abas->GetLocalWorkspaceMenuRoot())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports"));

	FofuxoVisorDaFonte::Registrados.AddUnique(Abas);
}

void FFofuxoVisorDaFonte::Abrir(FIKRetargetEditor& Editor)
{
	GarantirAba(Editor);

	if (const TSharedPtr<FTabManager> Abas = Editor.GetTabManager())
	{
		Abas->TryInvokeTab(FTabId(IdDaAba));
	}
}

void FFofuxoVisorDaFonte::Esquecer()
{
	for (const TWeakPtr<FTabManager>& Fraco : FofuxoVisorDaFonte::Registrados)
	{
		const TSharedPtr<FTabManager> Abas = Fraco.Pin();
		if (!Abas.IsValid())
		{
			continue;
		}

		// Fechar antes de desregistrar: o widget dentro dela e desta DLL.
		if (const TSharedPtr<SDockTab> Aba = Abas->FindExistingLiveTab(FTabId(IdDaAba)))
		{
			Aba->RequestCloseTab();
		}

		Abas->UnregisterTabSpawner(IdDaAba);
	}

	FofuxoVisorDaFonte::Registrados.Reset();
}

#undef LOCTEXT_NAMESPACE
