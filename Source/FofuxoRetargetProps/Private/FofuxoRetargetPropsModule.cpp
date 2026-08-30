// Fofuxo -- anexos de preview no IK Retargeter
//
// O visor do IK Retargeter nao pendura anexo nenhum: ele monta os componentes de
// preview na mao e nao passa pelo FAnimationEditorPreviewScene, que e quem faz
// isso na Persona. Este modulo faz esse trabalho nos dois bonecos.
//
// A lista vem do FFofuxoAnexosOp, que mora dentro do proprio retargeter. Ja
// existiu aqui um segundo caminho, lendo os "Add Preview Asset" da USkeletalMesh
// e da USkeleton -- e ele saiu justamente pelo que motivou o op: reimportar o rig
// como asset novo troca as duas, e tudo que estava preso nelas fica para tras. O
// retargeter nao troca.
//
// Ligar e desligar tambem e do op: e o Enable Op dele, na pilha. Nao ha botao na
// barra para isso, porque seriam dois interruptores para a mesma luz.

#include "FofuxoAjusteRodando.h"
#include "FofuxoAnexoDetalhes.h"
#include "FofuxoAnexosOp.h"
#include "FofuxoCopiarPose.h"
#include "FofuxoDetalhesDoOsso.h"
#include "FofuxoEspelhoDePose.h"
#include "FofuxoEsticarOssos.h"
#include "FofuxoOssosNaTela.h"
#include "FofuxoRefazerRetarget.h"
#include "FofuxoVisorDaFonte.h"
#include "FofuxoZerarRotacao.h"

#include "Animation/DebugSkelMeshComponent.h"
#include "ComponentAssetBroker.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Framework/Commands/UIAction.h"
#include "GameFramework/WorldSettings.h"
#include "Modules/ModuleManager.h"
#include "Retargeter/IKRetargeter.h"
#include "StructUtils/InstancedStruct.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ToolMenus.h"
#include "ToolMenuContext.h"
#include "ToolMenuSection.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

namespace FofuxoRetargetProps
{
	// FAssetEditorToolkit::GetEditorName() do editor de retarget.
	static const FName NomeDoEditor("IKRetargetEditor");

	// FWorkflowCentricApplication cola o nome do modo no fim do nome da barra:
	// "AssetEditor." + GetToolMenuAppName() + ".ToolBar" + "." + modo.
	static const FName NomeDaBarra("AssetEditor.IKRetargetEditor.ToolBar.IKRetargetApplicationMode");

	/** O editor de retarget dono desta barra, ou nullptr se a barra nao for de um. */
	static FIKRetargetEditor* EditorDoContexto(const FToolMenuContext& Contexto)
	{
		const UAssetEditorToolkitMenuContext* DoEditor = Contexto.FindContext<UAssetEditorToolkitMenuContext>();
		if (DoEditor == nullptr)
		{
			return nullptr;
		}

		const TSharedPtr<FAssetEditorToolkit> Toolkit = DoEditor->Toolkit.Pin();
		if (!Toolkit.IsValid() || Toolkit->GetEditorName() != NomeDoEditor)
		{
			return nullptr;
		}

		return static_cast<FIKRetargetEditor*>(Toolkit.Get());
	}

	/** A lista de anexos guardada neste retargeter, ou nullptr se ele nao tem o op. */
	static const FFofuxoAnexosOpSettings* AnexosDoRetargeter(const UIKRetargeter* Retargeter)
	{
		if (Retargeter == nullptr)
		{
			return nullptr;
		}

		for (const FInstancedStruct& Op : Retargeter->GetRetargetOps())
		{
			if (const FFofuxoAnexosOp* Nosso = Op.GetPtr<FFofuxoAnexosOp>())
			{
				return &Nosso->Settings;
			}
		}

		return nullptr;
	}

	/**
	 * Um numero que muda quando a lista muda.
	 *
	 * Editar a lista no painel de detalhes nao avisa ninguem, e refazer os
	 * componentes a cada meio segundo faria a arma piscar. Entao o tick compara
	 * este resumo com o do passeio anterior.
	 *
	 * O Enable Op entra aqui, e nao num "if" a parte: desligar o op e uma mudanca
	 * como outra qualquer, e o mesmo mecanismo que reage a uma linha nova reage a
	 * ele -- solta tudo e nao pendura de volta.
	 */
	static uint32 AssinaturaDosAnexos(const UIKRetargeter* Retargeter)
	{
		const FFofuxoAnexosOpSettings* Lista = AnexosDoRetargeter(Retargeter);
		if (Lista == nullptr || !Lista->bEnabled)
		{
			return 0;
		}

		uint32 Resumo = GetTypeHash(Lista->Anexos.Num());

		for (const FFofuxoAnexo& Anexo : Lista->Anexos)
		{
			Resumo = HashCombine(Resumo, GetTypeHash(Anexo.bMostrar));
			Resumo = HashCombine(Resumo, GetTypeHash(Anexo.OssoNaFonte.BoneName));
			Resumo = HashCombine(Resumo, GetTypeHash(Anexo.OssoNoAlvo.BoneName));
			Resumo = HashCombine(Resumo, GetTypeHash(Anexo.Asset.ToString()));
			Resumo = HashCombine(Resumo, GetTypeHash(static_cast<uint8>(Anexo.Boneco)));
			// Pelo texto, e nao pelos componentes: FRotator nao tem GetTypeHash, e
			// esta lista tem meia duzia de linhas conferidas duas vezes por segundo.
			Resumo = HashCombine(Resumo, GetTypeHash(Anexo.Encaixe.ToString()));
		}

		return Resumo;
	}
}

/**
 * Acompanha os editores de retarget abertos e mantem os anexos pendurados.
 *
 * Nao ha evento nem para "o retargeter trocou a preview mesh" nem para "a lista
 * do op mudou" que de para escutar de fora, e o editor recria os componentes de
 * preview quando a malha troca. Entao o estado e reconferido por ticker:
 * guardamos de qual componente, de qual malha e de qual lista os anexos
 * nasceram, e refazemos quando alguma das tres muda.
 */
class FGerenteDeAnexos
{
public:
	void Iniciar()
	{
		Ticker = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FGerenteDeAnexos::Tick), 0.5f);
	}

	void Encerrar()
	{
		if (Ticker.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
			Ticker.Reset();
		}

		for (FEditorAberto& Aberto : Abertos)
		{
			Soltar(Aberto);
		}
		Abertos.Reset();

		FFofuxoRefazerRetarget::Esquecer();
	}

	/** Quem recebe os editores que o tick encontra. O modulo e o dono. */
	void Avisar(FFofuxoEspelhoDePose* AoEspelho) { Espelho = AoEspelho; }

private:
	struct FEditorAberto
	{
		TWeakObjectPtr<UIKRetargeter> Asset;

		// De onde os anexos vieram, para saber quando refazer.
		TWeakObjectPtr<UDebugSkelMeshComponent> ComponenteFonte;
		TWeakObjectPtr<UDebugSkelMeshComponent> ComponenteAlvo;
		TWeakObjectPtr<USkeletalMesh> MalhaFonte;
		TWeakObjectPtr<USkeletalMesh> MalhaAlvo;

		// O resumo da lista do op no passeio anterior.
		uint32 Assinatura = 0;

		// Nao precisam de referencia forte: o pai guarda cada um em
		// USceneComponent::AttachChildren, que e UPROPERTY, e o pai vive
		// enquanto a cena de preview viver. Mesma aposta que a Persona faz.
		TArray<TWeakObjectPtr<USceneComponent>> Anexos;
	};

	bool Tick(float)
	{
		if (GEditor == nullptr)
		{
			return true;
		}

		UAssetEditorSubsystem* Subsistema = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (Subsistema == nullptr)
		{
			return true;
		}

		// Editores fechados levam a cena de preview inteira junto, entao nao ha o
		// que soltar -- so descartar a entrada.
		Abertos.RemoveAll([](const FEditorAberto& Aberto) { return !Aberto.Asset.IsValid(); });

		for (UObject* Editado : Subsistema->GetAllEditedAssets())
		{
			UIKRetargeter* Retargeter = Cast<UIKRetargeter>(Editado);
			if (Retargeter == nullptr)
			{
				continue;
			}

			IAssetEditorInstance* Instancia = Subsistema->FindEditorForAsset(Editado, false);
			if (Instancia == nullptr || Instancia->GetEditorName() != FofuxoRetargetProps::NomeDoEditor)
			{
				continue;
			}

			// GetEditorName() so devolve esse nome vindo do FIKRetargetEditor, que
			// e FAssetEditorToolkit. Os dois passos do cast sao heranca simples.
			FAssetEditorToolkit* Toolkit = static_cast<FAssetEditorToolkit*>(Instancia);
			FIKRetargetEditor* Editor = static_cast<FIKRetargetEditor*>(Toolkit);

			FEditorAberto* Aberto = Abertos.FindByPredicate(
				[Retargeter](const FEditorAberto& Candidato) { return Candidato.Asset == Retargeter; });

			if (Aberto == nullptr)
			{
				Aberto = &Abertos.AddDefaulted_GetRef();
				Aberto->Asset = Retargeter;
			}

			Sincronizar(*Aberto, *Editor);

			// De carona no mesmo passeio pelos editores abertos: o botao de
			// refazer tambem so pode ser posto depois que a aba dele existe, e o
			// espelho precisa saber que este editor abriu.
			FFofuxoRefazerRetarget::GarantirBotao(*Editor);
			FFofuxoAjusteRodando::Acompanhar(*Editor);
			FFofuxoZerarRotacao::GarantirAtalho(*Editor);
			FFofuxoVisorDaFonte::GarantirAba(*Editor);
			FFofuxoOssosNaTela::Acompanhar(*Editor);

			if (Espelho != nullptr)
			{
				Espelho->Acompanhar(*Editor);
			}
		}

		// Depois de todos: o aviso de desfazer vale para um passeio so.
		FFofuxoAjusteRodando::LimparAvisoDeDesfazer();

		return true;
	}

	void Sincronizar(FEditorAberto& Aberto, FIKRetargetEditor& Editor)
	{
		const TSharedRef<FIKRetargetEditorController> Controlador = Editor.GetController();

		UDebugSkelMeshComponent* Fonte = Controlador->SourceSkelMeshComponent;
		UDebugSkelMeshComponent* Alvo = Controlador->TargetSkelMeshComponent;

		USkeletalMesh* MalhaFonte = Fonte ? Fonte->GetSkeletalMeshAsset() : nullptr;
		USkeletalMesh* MalhaAlvo = Alvo ? Alvo->GetSkeletalMeshAsset() : nullptr;

		const uint32 Assinatura = FofuxoRetargetProps::AssinaturaDosAnexos(Aberto.Asset.Get());

		const bool bMudou =
			Aberto.ComponenteFonte != Fonte ||
			Aberto.ComponenteAlvo != Alvo ||
			Aberto.MalhaFonte != MalhaFonte ||
			Aberto.MalhaAlvo != MalhaAlvo ||
			Aberto.Assinatura != Assinatura;

		if (!bMudou)
		{
			return;
		}

		Soltar(Aberto);
		PendurarDoOp(Aberto, Aberto.Asset.Get(), Fonte, Alvo);

		Aberto.Assinatura = Assinatura;
		Aberto.ComponenteFonte = Fonte;
		Aberto.ComponenteAlvo = Alvo;
		Aberto.MalhaFonte = MalhaFonte;
		Aberto.MalhaAlvo = MalhaAlvo;
	}

	/** A lista que mora no retargeter. Cada linha diz em qual dos dois bonecos vai. */
	void PendurarDoOp(
		FEditorAberto& Aberto,
		const UIKRetargeter* Retargeter,
		UDebugSkelMeshComponent* Fonte,
		UDebugSkelMeshComponent* Alvo)
	{
		const FFofuxoAnexosOpSettings* Lista = FofuxoRetargetProps::AnexosDoRetargeter(Retargeter);
		if (Lista == nullptr || !Lista->bEnabled)
		{
			return;
		}

		for (const FFofuxoAnexo& Anexo : Lista->Anexos)
		{
			if (!Anexo.bMostrar)
			{
				continue;
			}

			// LoadSynchronous, e nao Get: a arma pode nao estar carregada, e este
			// e o momento em que se descobre que ela e necessaria. Uma vez so, mesmo
			// quando ela vai nos dois bonecos.
			UObject* Objeto = Anexo.Asset.LoadSynchronous();
			if (Objeto == nullptr)
			{
				continue;
			}

			// Um osso por lado: os dois esqueletos quase nunca chamam o mesmo osso
			// pelo mesmo nome, e e por isso que existe retarget.
			if (Anexo.Boneco != EFofuxoBoneco::Alvo)
			{
				PendurarNum(Aberto, Fonte, Objeto, Anexo, Anexo.OssoNaFonte.BoneName);
			}

			if (Anexo.Boneco != EFofuxoBoneco::Fonte)
			{
				PendurarNum(Aberto, Alvo, Objeto, Anexo, Anexo.OssoNoAlvo.BoneName);
			}
		}
	}

	/** Uma linha da lista, num boneco so. */
	void PendurarNum(
		FEditorAberto& Aberto,
		UDebugSkelMeshComponent* Componente,
		UObject* Objeto,
		const FFofuxoAnexo& Anexo,
		const FName Osso)
	{
		if (Componente == nullptr)
		{
			return;
		}

		UWorld* Mundo = Componente->GetWorld();
		AWorldSettings* Dono = Mundo ? Mundo->GetWorldSettings() : nullptr;
		if (Dono == nullptr)
		{
			return;
		}

		// O Anexar sai quieto quando o osso esta vazio ou nao existe deste lado --
		// linha pela metade nao vira erro, so nao aparece.
		Anexar(Aberto, Componente, Dono, Objeto, Osso, Anexo.Encaixe);
	}

	void Anexar(
		FEditorAberto& Aberto,
		UDebugSkelMeshComponent* Componente,
		AWorldSettings* Dono,
		UObject* Objeto,
		const FName Encaixe,
		const FTransform& Ajuste = FTransform::Identity)
	{
		if (Objeto == nullptr || Encaixe.IsNone())
		{
			return;
		}

		// DoesSocketExist cobre socket e osso. O ponto de encaixe pode nao existir
		// deste lado do retarget: o esqueleto do alvo raramente tem os mesmos
		// nomes do esqueleto da fonte.
		if (!Componente->DoesSocketExist(Encaixe))
		{
			return;
		}

		const TSubclassOf<UActorComponent> Classe =
			FComponentAssetBrokerage::GetPrimaryComponentForAsset(Objeto->GetClass());

		if (!*Classe || !Classe->IsChildOf(USceneComponent::StaticClass()))
		{
			return;
		}

		// RF_Transient, e nao RF_Transactional como na Persona: estes aqui sao
		// derivados do que ja esta salvo no op, nao ha o que desfazer nem o que
		// gravar.
		USceneComponent* Anexo = NewObject<USceneComponent>(Dono, Classe, NAME_None, RF_Transient);

		FComponentAssetBrokerage::AssignAssetToComponent(Anexo, Objeto);

		Anexo->SetupAttachment(Componente, Encaixe);
		Anexo->RegisterComponent();

		if (!Ajuste.Equals(FTransform::Identity))
		{
			Anexo->SetRelativeTransform(Ajuste);
		}

		Aberto.Anexos.Add(Anexo);
	}

	void Soltar(FEditorAberto& Aberto)
	{
		for (const TWeakObjectPtr<USceneComponent>& Fraco : Aberto.Anexos)
		{
			if (USceneComponent* Anexo = Fraco.Get())
			{
				Anexo->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
				Anexo->DestroyComponent();
			}
		}

		Aberto.Anexos.Reset();
	}

	TArray<FEditorAberto> Abertos;
	FTSTicker::FDelegateHandle Ticker;
	FFofuxoEspelhoDePose* Espelho = nullptr;
};

class FFofuxoRetargetPropsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		Espelho = MakeUnique<FFofuxoEspelhoDePose>();
		Espelho->Iniciar();

		Gerente = MakeUnique<FGerenteDeAnexos>();
		Gerente->Avisar(Espelho.Get());
		Gerente->Iniciar();

		FFofuxoAnexoDetalhes::Registrar();
		FFofuxoAjusteRodando::Registrar();
		FFofuxoDetalhesDoOsso::Registrar();
		FFofuxoZerarRotacao::Registrar();

		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FFofuxoRetargetPropsModule::RegistrarMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);

		// Antes de soltar o resto: o botao e as lambdas dele vivem nesta DLL, e um
		// painel que continuasse com eles depois do unload chamaria codigo que nao
		// existe mais.
		FFofuxoAnexoDetalhes::Esquecer();
		FFofuxoAjusteRodando::Esquecer();
		FFofuxoDetalhesDoOsso::Esquecer();
		FFofuxoZerarRotacao::Esquecer();
		FFofuxoVisorDaFonte::Esquecer();
		FFofuxoOssosNaTela::Esquecer();

		if (Gerente.IsValid())
		{
			Gerente->Encerrar();
			Gerente.Reset();
		}

		if (Espelho.IsValid())
		{
			Espelho->Encerrar();
			Espelho.Reset();
		}
	}

private:
	void RegistrarMenus()
	{
		FToolMenuOwnerScoped Dono(this);

		// ExtendMenu funciona antes de a barra existir: o editor de retarget so
		// registra a dele quando o primeiro asset abre.
		UToolMenu* Barra = UToolMenus::Get()->ExtendMenu(FofuxoRetargetProps::NomeDaBarra);
		if (Barra == nullptr)
		{
			return;
		}

		FToolMenuSection& Secao = Barra->FindOrAddSection("Fofuxo");

		// Os anexos nao tem botao aqui: quem liga e desliga e o Enable Op do
		// "Anexos de Preview (Fofuxo)", na pilha de ops, que e onde a lista mora.

		Secao.AddEntry(FToolMenuEntry::InitToolBarButton(
			"FofuxoAjusteRodando",
			FUIAction(
				FExecuteAction::CreateStatic(&FFofuxoAjusteRodando::Alternar),
				FCanExecuteAction(),
				FIsActionChecked::CreateStatic(&FFofuxoAjusteRodando::EstaLigado)),
			LOCTEXT("AjusteRodando", "Live Retarget"),
			LOCTEXT("AjusteRodandoTip",
				"Poe um gizmo de rotacao no Running Retarget: com a animacao parada no frame que voce "
				"quiser, clique num osso do alvo e gire. Feito para os dedos, que no ref pose estao "
				"abertos e nao mostram se fecham na arma.\n\n"
				"So no alvo, e so com o botao Fonte/Alvo no alvo -- a animacao da fonte e o dado de "
				"entrada, nao ha o que ajustar nela.\n\n"
				"O que o gizmo escreve e a pose de retarget, nao um ajuste daquele frame: o retargeter "
				"nao tem onde guardar correcao por frame. Mas o giro que voce ver no frame 37 e o mesmo "
				"giro que sai em todos os outros -- para dedo isso e o certo, porque o erro de um dedo "
				"que segura uma espada e constante e o frame so serve para voce enxerga-lo."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.RotateMode"),
			EUserInterfaceActionType::ToggleButton));

		Secao.AddEntry(FToolMenuEntry::InitToolBarButton(
			"FofuxoVisorDaFonte",
			FToolUIActionChoice(FToolUIAction(
				FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& Contexto)
				{
					if (FIKRetargetEditor* Editor = FofuxoRetargetProps::EditorDoContexto(Contexto))
					{
						FFofuxoVisorDaFonte::Abrir(*Editor);
					}
				}))),
			LOCTEXT("VisorDaFonte", "Visor da fonte"),
			LOCTEXT("VisorDaFonteTip",
				"Abre um segundo visor da mesma cena, com a camera colada no osso da fonte que "
				"corresponde ao osso selecionado -- e seguindo ele quadro a quadro enquanto a "
				"animacao roda.\n\n"
				"Serve para ver o gabarito e o ajuste ao mesmo tempo: num visor voce gira o dedo "
				"do alvo, no outro voce ve como o dedo do Manny esta naquele mesmo quadro, sem "
				"viajar de camera entre os dois bonecos.\n\n"
				"O correspondente sai do mapeamento de cadeias. A aba tambem esta em Window, e "
				"nao volta sozinha quando o editor reabre."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports")));

		Secao.AddEntry(FToolMenuEntry::InitToolBarButton(
			"FofuxoOssosEmVareta",
			FUIAction(
				FExecuteAction::CreateStatic(&FFofuxoOssosNaTela::Alternar),
				FCanExecuteAction(),
				FIsActionChecked::CreateStatic(&FFofuxoOssosNaTela::EstaLigado)),
			LOCTEXT("OssosEmVareta", "Ossos em vareta"),
			LOCTEXT("OssosEmVaretaTip",
				"Troca o osso octaedrico da Unreal por uma vareta como a do Blender: linha fina "
				"entre as juntas e um circulo em cada uma, do mesmo tamanho na tela em qualquer "
				"distancia de camera. Numa mao com quinze ossos e a diferenca entre ver os dedos "
				"e ver uma bola cinza.\n\n"
				"O desenho da engine nao some, so encolhe -- e' nele que mora a identidade do osso "
				"para o clique. Ele fica escondido debaixo da vareta.\n\n"
				"O tamanho encolhido e o BoneDrawSize do retargeter, o mesmo da regua em "
				"Character > Bones. Desligar devolve o valor de antes; salvar o RTG com isto "
				"ligado grava o tamanho encolhido."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.WireframeMode"),
			EUserInterfaceActionType::ToggleButton));

		FFofuxoEspelhoDePose* AoEspelho = Espelho.Get();

		Secao.AddEntry(FToolMenuEntry::InitToolBarButton(
			"FofuxoEspelho",
			FUIAction(
				FExecuteAction::CreateLambda([AoEspelho]() { AoEspelho->Alternar(); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([AoEspelho]() { return AoEspelho->EstaLigado(); })),
			LOCTEXT("Espelho", "Espelhar"),
			LOCTEXT("EspelhoTip",
				"Repete no osso do outro lado a rotacao que voce der em um osso -- rodou o thigh_l, o "
				"thigh_r acompanha espelhado. Vale no Editing Retarget Pose e tambem no Live Retarget, "
				"com o gizmo ou com o Alt+R.\n\n"
				"Acha o par pelo nome: o lado escrito como l/r, left/right ou lt/rt, separado por \"_\", "
				"\".\", \"-\" ou espaco, em qualquer caixa, ou colado em camelCase (HandL). Osso sem par, "
				"como a coluna e a cabeca, fica de fora.\n\n"
				"Se voce mexer nos dois lados de uma vez -- os dois selecionados no gizmo, um Auto Align "
				"geral, um Ctrl+Z -- o espelho nao entra."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "UMGEditor.Mirror"),
			EUserInterfaceActionType::ToggleButton));

		// Rotulo, dica e icone sao atributos, e nao textos fixos: eles mudam com o
		// modo armado, que e como se ve na barra o que o clique vai fazer.
		Secao.AddEntry(FToolMenuEntry::InitToolBarButton(
			"FofuxoEsticar",
			FToolUIActionChoice(FToolUIAction(
				FToolMenuExecuteAction::CreateStatic(&FFofuxoEsticarOssos::Esticar),
				FToolMenuCanExecuteAction::CreateStatic(&FFofuxoEsticarOssos::Pode),
				FToolMenuGetActionCheckState())),
			TAttribute<FText>::CreateLambda(
				[]() { return FFofuxoEsticarOssos::Rotulo(FFofuxoEsticarOssos::Modo()); }),
			TAttribute<FText>::CreateLambda([]()
			{
				return FText::Format(
					LOCTEXT("EsticarTipComModo", "{0}\n\nNos tres pontos ao lado voce troca o que este botao faz."),
					FFofuxoEsticarOssos::Dica(FFofuxoEsticarOssos::Modo()));
			}),
			TAttribute<FSlateIcon>::CreateLambda(
				[]() { return FFofuxoEsticarOssos::Icone(FFofuxoEsticarOssos::Modo()); })));

		Secao.AddEntry(FToolMenuEntry::InitComboButton(
			"FofuxoEsticarOpcoes",
			FToolUIActionChoice(),
			FNewToolMenuChoice(FNewToolMenuDelegate::CreateStatic(&FFofuxoEsticarOssos::MontarMenuDeModos)),
			FText::GetEmpty(),
			LOCTEXT("EsticarOpcoesTip", "Escolher o que o botao Esticar faz."),
			FSlateIcon(),
			/*bSimpleComboBox*/ true));

		Secao.AddEntry(FToolMenuEntry::InitComboButton(
			"FofuxoCopiarPose",
			FToolUIActionChoice(FToolUIAction(
				FToolMenuExecuteAction(),
				FToolMenuCanExecuteAction::CreateStatic(&FFofuxoCopiarPose::Pode),
				FToolMenuGetActionCheckState())),
			FNewToolMenuChoice(FNewToolMenuDelegate::CreateStatic(&FFofuxoCopiarPose::MontarMenu)),
			LOCTEXT("CopiarPose", "Copiar pose"),
			LOCTEXT("CopiarPoseTipBarra",
				"Traz para o lado que voce esta editando a pose de retarget de outro retargeter, "
				"casando os ossos pelo nome.\n\n"
				"Serve para o conserto que nao viaja: se todo retarget do projeto sai do mesmo boneco, "
				"o lado fonte de todos eles tem a mesma pose, e ajustar um nao ajusta os outros. Do lado "
				"alvo vale igual entre personagens que seguem a convencao de nomes da Unreal.\n\n"
				"A pose daqui e substituida, nao somada, e a pergunta antes de escrever diz quantos ossos "
				"batem, quantos ficam de fora e quantos voltam para o ref pose."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Clipboard")));
	}

	TUniquePtr<FGerenteDeAnexos> Gerente;
	TUniquePtr<FFofuxoEspelhoDePose> Espelho;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFofuxoRetargetPropsModule, FofuxoRetargetProps)
