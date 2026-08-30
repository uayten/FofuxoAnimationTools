// Fofuxo -- girar osso do alvo com a animacao rodando

#include "FofuxoAjusteRodando.h"

#include "FofuxoAnexosOp.h"
#include "FofuxoOssosNaTela.h"
#include "FofuxoZerarRotacao.h"

#include "Animation/DebugSkelMeshComponent.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "EditorModeRegistry.h"
#include "EditorUndoClient.h"
#include "EditorViewportClient.h"
#include "Framework/Commands/UICommandList.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/ConfigCacheIni.h"
#include "ReferenceSkeleton.h"
#include "RetargetEditor/IKRetargetCommands.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoAjuste, Log, All);

const FEditorModeID FFofuxoAjusteRodando::Id("FofuxoAjusteRodando");

namespace FofuxoAjuste
{
	static const TCHAR* SecaoIni = TEXT("FofuxoRetargetProps");
	static const TCHAR* ChaveIni = TEXT("AjustarRodando");

	static bool bLidoDoIni = false;
	static bool bLigado = false;

	/**
	 * Sabe que um Ctrl+Z acabou de acontecer.
	 *
	 * O PostUndo do retargeter refaz as preview meshes, reinicializa o processador e
	 * mexe no playback, e nesse caminho o editor as vezes cai de volta no Editing
	 * Retarget Pose com o interruptor ainda ligado. Nao da para consertar de dentro:
	 * o SetRetargeterMode e todos os vizinhos dele sao da IKRigEditor e nao sao
	 * exportados. O que sobra e o comando da barra, que e publico -- mas ele so pode
	 * ser disparado quando o modo mudou *sozinho*, e nao quando foi voce que trocou.
	 */
	class FVigiaDesfazer : public FSelfRegisteringEditorUndoClient
	{
	public:
		virtual void PostUndo(bool) override { bAconteceu = true; }
		virtual void PostRedo(bool) override { bAconteceu = true; }

		bool bAconteceu = false;
	};

	static TUniquePtr<FVigiaDesfazer> Vigia;

	/** Em que modo cada retargeter estava no passeio anterior. */
	static TMap<TWeakObjectPtr<UIKRetargeter>, uint8> ModoAnterior;
}

void FFofuxoAjusteRodando::Registrar()
{
	// O bVisible e false: este modo nao e para aparecer na barra de modos do
	// editor de nivel, ele so existe dentro do editor de retarget. E' o mesmo que
	// a IKRigEditor faz com os dois modos dela.
	FofuxoAjuste::Vigia = MakeUnique<FofuxoAjuste::FVigiaDesfazer>();

	FEditorModeRegistry::Get().RegisterMode<FFofuxoAjusteRodando>(
		Id,
		LOCTEXT("AjusteRodandoModo", "Live Retarget"),
		FSlateIcon(),
		/*bVisible*/ false);
}

void FFofuxoAjusteRodando::Esquecer()
{
	FEditorModeRegistry::Get().UnregisterMode(Id);

	FofuxoAjuste::Vigia.Reset();
	FofuxoAjuste::ModoAnterior.Reset();
}

bool FFofuxoAjusteRodando::EstaLigado()
{
	if (!FofuxoAjuste::bLidoDoIni)
	{
		FofuxoAjuste::bLidoDoIni = true;
		GConfig->GetBool(FofuxoAjuste::SecaoIni, FofuxoAjuste::ChaveIni,
			FofuxoAjuste::bLigado, GEditorPerProjectIni);
	}

	return FofuxoAjuste::bLigado;
}

void FFofuxoAjusteRodando::Alternar()
{
	// A leitura preguicosa antes: sem isto a primeira consulta depois desta iria
	// ao ini e desfaria a escolha.
	EstaLigado();

	FofuxoAjuste::bLigado = !FofuxoAjuste::bLigado;

	GConfig->SetBool(FofuxoAjuste::SecaoIni, FofuxoAjuste::ChaveIni,
		FofuxoAjuste::bLigado, GEditorPerProjectIni);
}

void FFofuxoAjusteRodando::LimparAvisoDeDesfazer()
{
	if (FofuxoAjuste::Vigia.IsValid())
	{
		FofuxoAjuste::Vigia->bAconteceu = false;
	}

	// Retargeter fechado sai da conta -- senao o mapa cresce por toda a sessao.
	for (auto Passo = FofuxoAjuste::ModoAnterior.CreateIterator(); Passo; ++Passo)
	{
		if (!Passo.Key().IsValid())
		{
			Passo.RemoveCurrent();
		}
	}
}

void FFofuxoAjusteRodando::Acompanhar(FIKRetargetEditor& Editor)
{
	const TSharedRef<FIKRetargetEditorController> Controlador = Editor.GetController();

	UIKRetargeter* Asset = Controlador->AssetController != nullptr
		? Controlador->AssetController->GetAsset()
		: nullptr;

	ERetargeterOutputMode Modo = Controlador->GetRetargeterMode();

	// Voltar so quando o modo mudou sozinho: se foi voce que clicou em Editing
	// Retarget Pose e depois deu um Ctrl+Z, a troca foi sua e fica de pe.
	const uint8* Antes = Asset != nullptr ? FofuxoAjuste::ModoAnterior.Find(Asset) : nullptr;

	const bool bCaiuSozinho = FofuxoAjuste::Vigia.IsValid()
		&& FofuxoAjuste::Vigia->bAconteceu
		&& EstaLigado()
		&& Modo == ERetargeterOutputMode::EditRetargetPose
		&& Antes != nullptr
		&& static_cast<ERetargeterOutputMode>(*Antes) == ERetargeterOutputMode::RunRetarget;

	if (bCaiuSozinho)
	{
		// Pelo comando da barra, que e o unico caminho publico para o
		// SetRetargeterMode.
		Editor.GetToolkitCommands()->TryExecuteAction(
			FIKRetargetCommands::Get().RunRetargeter.ToSharedRef());

		Modo = Controlador->GetRetargeterMode();
	}

	if (Asset != nullptr)
	{
		FofuxoAjuste::ModoAnterior.Add(Asset, static_cast<uint8>(Modo));
	}

	// O modo entra sempre que o editor esta aberto, e nao so no Live Retarget: quem
	// desenha as varetas e quem apanha o clique por proximidade e este modo, e as
	// duas coisas valem nos dois modos do retargeter.
	//
	// O gizmo, esse continua so no Live Retarget -- quem o segura e o Juntar(), que
	// recusa fora dele. No Editing Retarget Pose quem manda e o gizmo da propria
	// engine, e dois gizmos na mesma tela seriam duas respostas para o mesmo
	// arrasto.
	constexpr bool bQuero = true;

	FEditorModeTools& Modos = Editor.GetEditorModeManager();
	const bool bEsta = Modos.IsModeActive(Id);

	if (bQuero == bEsta)
	{
		return;
	}

	if (!bQuero)
	{
		Modos.DeactivateMode(Id);
		return;
	}

	Modos.ActivateMode(Id);

	if (FFofuxoAjusteRodando* Meu = Modos.GetActiveModeTyped<FFofuxoAjusteRodando>(Id))
	{
		Meu->Apontar(Controlador.ToSharedPtr());
	}
}

bool FFofuxoAjusteRodando::Juntar(TArray<FEscolhido>& OutEscolhidos) const
{
	const TSharedPtr<FIKRetargetEditorController> Quem = Controlador.Pin();
	if (!Quem.IsValid())
	{
		return false;
	}

	// O modo fica ativo o tempo todo, por causa do desenho e do clique. O gizmo,
	// nao: fora do Live Retarget quem manda e o gizmo da engine. Este e o unico
	// lugar que precisa dizer isso -- todo o caminho do gizmo passa por aqui.
	if (!EstaLigado() || Quem->GetRetargeterMode() != ERetargeterOutputMode::RunRetarget)
	{
		return false;
	}

	// So o alvo. A animacao da fonte e o dado de entrada -- nao ha nada a ajustar
	// nela, e deixar o gizmo aparecer la seria convidar a estragar a referencia.
	if (Quem->GetSourceOrTarget() != ERetargetSourceOrTarget::Target)
	{
		return false;
	}

	UIKRetargeterController* AssetController = Quem->AssetController;
	UDebugSkelMeshComponent* Componente = Quem->TargetSkelMeshComponent;

	if (AssetController == nullptr || Componente == nullptr)
	{
		return false;
	}

	USkeletalMesh* Malha = Componente->GetSkeletalMeshAsset();
	if (Malha == nullptr)
	{
		return false;
	}

	const FReferenceSkeleton& Esqueleto = Malha->GetRefSkeleton();

	const TMap<FName, FQuat>& Deltas =
		AssetController->GetCurrentRetargetPose(ERetargetSourceOrTarget::Target).GetAllDeltaRotations();

	for (const FName& Osso : Quem->GetSelectedBones())
	{
		const int32 Indice = Esqueleto.FindBoneIndex(Osso);
		if (Indice == INDEX_NONE)
		{
			continue;
		}

		FEscolhido Escolhido;
		Escolhido.Osso = Osso;

		// Em espaco de mundo, que e onde o gizmo trabalha. A rotacao do componente
		// se cancela na conta do arrasto, entao nao ha erro em misturar os dois.
		Escolhido.NoMundo = Componente->GetBoneTransform(Indice);

		if (const FQuat* Achado = Deltas.Find(Osso))
		{
			Escolhido.DeltaAoIniciar = *Achado;
		}

		OutEscolhidos.Add(Escolhido);
	}

	return !OutEscolhidos.IsEmpty();
}

bool FFofuxoAjusteRodando::AcharLinha(FLinha& OutLinha) const
{
	TArray<FEscolhido> Escolhidos;
	if (!Juntar(Escolhidos))
	{
		return false;
	}

	const TSharedPtr<FIKRetargetEditorController> Quem = Controlador.Pin();
	UIKRetargeter* Asset = Quem.IsValid() && Quem->AssetController != nullptr
		? Quem->AssetController->GetAsset()
		: nullptr;

	if (Asset == nullptr)
	{
		return false;
	}

	FFofuxoAnexosOp* Op = Asset->GetFirstRetargetOpOfType<FFofuxoAnexosOp>();
	if (Op == nullptr)
	{
		return false;
	}

	// Pelo ultimo clicado, que e o mesmo osso em que o gizmo esta desenhado.
	const FEscolhido& Ultimo = Escolhidos.Last();

	for (int32 Linha = 0; Linha < Op->Settings.Anexos.Num(); ++Linha)
	{
		const FFofuxoAnexo& Anexo = Op->Settings.Anexos[Linha];

		if (Anexo.Boneco == EFofuxoBoneco::Fonte || Anexo.OssoNoAlvo.BoneName != Ultimo.Osso)
		{
			continue;
		}

		OutLinha.Op = Op;
		OutLinha.Indice = Linha;
		OutLinha.NoMundo = Ultimo.NoMundo;
		OutLinha.AoIniciar = Anexo.Deslocamento;

		return true;
	}

	return false;
}

bool FFofuxoAjusteRodando::HandleClick(
	FEditorViewportClient* InViewportClient,
	HHitProxy* HitProxy,
	const FViewportClick& Click)
{
	if (InViewportClient == nullptr || Click.GetKey() != EKeys::LeftMouseButton)
	{
		return false;
	}

	// Acertou o osso na mosca: o modo da engine ja o selecionou, e uma segunda
	// busca so poderia discordar dele.
	if (FFofuxoOssosNaTela::EhDeOsso(HitProxy))
	{
		return false;
	}

	FViewport* Visor = InViewportClient->Viewport;
	if (Visor == nullptr)
	{
		return false;
	}

	HHitProxy* Perto = FFofuxoOssosNaTela::OssoPertoDoCursor(
		*Visor, Visor->GetMouseX(), Visor->GetMouseY());

	if (Perto == nullptr)
	{
		return false;
	}

	FEditorModeTools* Modos = GetModeManager();
	if (Modos == nullptr)
	{
		return false;
	}

	// Quem seleciona continua sendo o modo da engine: entregamos a ele o proxy que
	// o clique errou por pouco, e ele faz o resto -- selecao, painel de detalhes,
	// hierarquia. A chamada e virtual, entao nao precisa de simbolo exportado
	// nenhum da IKRigEditor; e como o modo dela roda antes do nosso e ja limpou a
	// selecao, escrever agora e a ultima palavra.
	for (const FEditorModeID& Dela : {
		FEditorModeID("IKRetargetAssetDefaultMode"),
		FEditorModeID("IKRetargetAssetEditMode")})
	{
		if (FEdMode* Modo = Modos->GetActiveMode(Dela))
		{
			return Modo->HandleClick(InViewportClient, Perto, Click);
		}
	}

	return false;
}

void FFofuxoAjusteRodando::Render(
	const FSceneView* View,
	FViewport* Viewport,
	FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);

	if (const TSharedPtr<FIKRetargetEditorController> Quem = Controlador.Pin())
	{
		FFofuxoOssosNaTela::Desenhar(*Quem, View, PDI);
	}
}

bool FFofuxoAjusteRodando::UsesTransformWidget() const
{
	TArray<FEscolhido> Escolhidos;
	return Juntar(Escolhidos);
}

bool FFofuxoAjusteRodando::UsesTransformWidget(const UE::Widget::EWidgetMode CheckMode) const
{
	if (CheckMode == UE::Widget::WM_Rotate)
	{
		// Rotacao vai para a pose de retarget, e vale para qualquer osso do alvo.
		return UsesTransformWidget();
	}

	if (CheckMode == UE::Widget::WM_Translate)
	{
		// Mover so vale onde ha onde guardar: a pose de retarget nao tem translacao
		// por osso, entao o destino e o Deslocamento da linha do op dos anexos, e o
		// gizmo so aparece num osso que alguma linha nomeie.
		FLinha Qual;
		return AcharLinha(Qual);
	}

	return false;
}

bool FFofuxoAjusteRodando::ShouldDrawWidget() const
{
	return UsesTransformWidget();
}

FVector FFofuxoAjusteRodando::GetWidgetLocation() const
{
	TArray<FEscolhido> Escolhidos;
	if (!Juntar(Escolhidos))
	{
		return FVector::ZeroVector;
	}

	// No ultimo clicado, como a engine faz: a lista de selecao cresce pelo fim.
	return Escolhidos.Last().NoMundo.GetLocation();
}

bool FFofuxoAjusteRodando::GetCustomDrawingCoordinateSystem(FMatrix& OutMatrix, void*)
{
	TArray<FEscolhido> Escolhidos;
	if (!Juntar(Escolhidos))
	{
		return false;
	}

	OutMatrix = FRotationMatrix::Make(Escolhidos.Last().NoMundo.GetRotation());
	return true;
}

bool FFofuxoAjusteRodando::GetCustomInputCoordinateSystem(FMatrix& OutMatrix, void* InData)
{
	return GetCustomDrawingCoordinateSystem(OutMatrix, InData);
}

bool FFofuxoAjusteRodando::StartTracking(FEditorViewportClient* InViewportClient, FViewport*)
{
	Arrastando.Reset();
	Acumulado = FQuat::Identity;
	Movendo = FLinha();
	AcumuladoDeMover = FVector::ZeroVector;

	// Sem um eixo do gizmo agarrado, este arrasto nao e nosso -- e a camera. Dizer
	// que sim aqui e o que engolia o alt+clique, o botao do meio e o botao direito:
	// o StartTracking e chamado para *todo* arrasto no visor, e quem devolve true
	// esta dizendo "eu estou manipulando alguma coisa, nao mexa a camera".
	if (InViewportClient == nullptr || InViewportClient->GetCurrentWidgetAxis() == EAxisList::None)
	{
		return false;
	}

	const bool bMovendo = InViewportClient->GetWidgetMode() == UE::Widget::WM_Translate;

	if (bMovendo ? !AcharLinha(Movendo) : !Juntar(Arrastando))
	{
		return false;
	}

	const TSharedPtr<FIKRetargetEditorController> Quem = Controlador.Pin();
	UIKRetargeter* Asset = Quem.IsValid() && Quem->AssetController != nullptr
		? Quem->AssetController->GetAsset()
		: nullptr;

	if (Asset == nullptr)
	{
		Arrastando.Reset();
		Movendo = FLinha();
		return false;
	}

	// Uma transacao para o arrasto inteiro, aberta na mao em vez de por
	// FScopedTransaction: ela comeca aqui e so fecha no EndTracking.
	GEditor->BeginTransaction(bMovendo
		? LOCTEXT("MoverRodandoTransacao", "Mover osso com a animacao rodando")
		: LOCTEXT("AjusteRodandoTransacao", "Girar osso com a animacao rodando"));
	Asset->Modify();
	bEmTransacao = true;

	return true;
}

bool FFofuxoAjusteRodando::InputKey(
	FEditorViewportClient* InViewportClient,
	FViewport* InViewport,
	FKey InKey,
	EInputEvent InEvent)
{
	// O Alt+R tambem esta na lista de comandos do toolkit, que e o que o faz valer
	// com o foco na hierarquia ou na pilha de ops. Aqui ele e apanhado mais cedo,
	// no visor, porque o FEditorModeTools ve a tecla antes de ela subir -- sem
	// isto, o mesmo atalho funcionaria em todo lugar menos onde voce esta olhando.
	if (InEvent == IE_Pressed
		&& InKey == EKeys::R
		&& InViewport != nullptr
		&& (InViewport->KeyState(EKeys::LeftAlt) || InViewport->KeyState(EKeys::RightAlt)))
	{
		if (FFofuxoZerarRotacao::Pode(Controlador))
		{
			FFofuxoZerarRotacao::Zerar(Controlador);
			return true;
		}
	}

	return FEdMode::InputKey(InViewportClient, InViewport, InKey, InEvent);
}

bool FFofuxoAjusteRodando::EndTracking(FEditorViewportClient*, FViewport*)
{
	if (bEmTransacao)
	{
		GEditor->EndTransaction();
		bEmTransacao = false;
	}

	const bool bMexeu = !Arrastando.IsEmpty() || Movendo.Op != nullptr;

	Arrastando.Reset();
	Acumulado = FQuat::Identity;
	Movendo = FLinha();
	AcumuladoDeMover = FVector::ZeroVector;

	return bMexeu;
}

bool FFofuxoAjusteRodando::InputDelta(
	FEditorViewportClient* InViewportClient,
	FViewport*,
	FVector& InDrag,
	FRotator& InRot,
	FVector&)
{
	if (Movendo.Op != nullptr && InViewportClient->GetWidgetMode() == UE::Widget::WM_Translate)
	{
		AcumuladoDeMover += InDrag;

		if (!Movendo.Op->Settings.Anexos.IsValidIndex(Movendo.Indice))
		{
			return false;
		}

		// O arrasto chega em espaco de mundo, e o Deslocamento e dito no frame do
		// osso -- e assim que o Run() do op vai le-lo de volta.
		const FVector NoOsso = Movendo.NoMundo.GetRotation().UnrotateVector(AcumuladoDeMover);

		FFofuxoAnexosOp* Op = Movendo.Op;
		Op->Settings.Anexos[Movendo.Indice].Deslocamento = Movendo.AoIniciar + NoOsso;

#if WITH_EDITORONLY_DATA
		// A copia que esta rodando no visor e outra: sem isto o valor so apareceria
		// na proxima reinicializacao do retargeter, e o arrasto seria as cegas.
		if (FIKRetargetOpSettingsBase* Rodando = Op->Settings.EditorInstance)
		{
			Rodando->CopySettingsAtRuntime(&Op->Settings);
		}
#endif

		return true;
	}

	if (Arrastando.IsEmpty() || InViewportClient->GetWidgetMode() != UE::Widget::WM_Rotate)
	{
		return false;
	}

	const TSharedPtr<FIKRetargetEditorController> Quem = Controlador.Pin();
	if (!Quem.IsValid() || Quem->AssetController == nullptr)
	{
		return false;
	}

	// O giro acumulado desde o inicio do arrasto, em espaco de mundo.
	Acumulado = InRot.Quaternion() * Acumulado;

	const FVector Eixo = Acumulado.GetRotationAxis();
	const float Angulo = Acumulado.GetAngle();

	for (const FEscolhido& Escolhido : Arrastando)
	{
		// O mesmo giro, dito no frame do osso. E' a conjugacao do giro do mundo
		// pela transformada do osso -- e e por ela que pos-multiplicar isto na pose
		// de retarget produz, na saida, exatamente o giro que voce fez na tela.
		const FVector NoOsso = Escolhido.NoMundo.InverseTransformVector(Eixo);
		const FQuat Novo = (Escolhido.DeltaAoIniciar * FQuat(NoOsso, Angulo)).GetNormalized();

		Quem->AssetController->SetRotationOffsetForRetargetPoseBone(
			Escolhido.Osso, Novo, ERetargetSourceOrTarget::Target);
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
