// Fofuxo's Exporter -- anexos de preview no IK Retargeter
//
// O editor de esqueleto guarda os "Add Preview Asset" em dois lugares:
// USkeletalMesh::GetPreviewAttachedAssetContainer() e
// USkeleton::PreviewAttachedAssetContainer. O visor do IK Retargeter ignora os
// dois -- ele monta os componentes de preview na mao e nao passa pelo
// FAnimationEditorPreviewScene, que e quem faz isso na Persona.
//
// Este modulo repete, no visor do retargeter, o que a
// FAnimationEditorPreviewScene::AddPreviewAttachedObjects faz na Persona, nos
// dois bonecos ao mesmo tempo.

#include "FofuxoCopiarPose.h"
#include "FofuxoEspelhoDePose.h"
#include "FofuxoEsticarOssos.h"
#include "FofuxoRefazerRetarget.h"

#include "Animation/DebugSkelMeshComponent.h"
#include "Animation/PreviewAssetAttachComponent.h"
#include "Animation/Skeleton.h"
#include "ComponentAssetBroker.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Framework/Commands/UIAction.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Retargeter/IKRetargeter.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "ToolMenus.h"
#include "Toolkits/AssetEditorToolkit.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

namespace FofuxoRetargetProps
{
	// FAssetEditorToolkit::GetEditorName() do editor de retarget.
	static const FName NomeDoEditor("IKRetargetEditor");

	// FWorkflowCentricApplication cola o nome do modo no fim do nome da barra:
	// "AssetEditor." + GetToolMenuAppName() + ".ToolBar" + "." + modo.
	static const FName NomeDaBarra("AssetEditor.IKRetargetEditor.ToolBar.IKRetargetApplicationMode");

	static const TCHAR* SecaoIni = TEXT("FofuxoRetargetProps");
	static const TCHAR* ChaveIni = TEXT("MostrarAnexos");
}

/**
 * Acompanha os editores de retarget abertos e mantem os anexos pendurados.
 *
 * Nao ha evento para "o retargeter trocou a preview mesh" que de para escutar de
 * fora, e o editor recria os componentes de preview quando isso acontece. Entao
 * o estado e reconferido por ticker: guardamos de qual componente e de qual
 * malha os anexos nasceram, e refazemos quando algum dos dois muda.
 */
class FGerenteDeAnexos
{
public:
	void Iniciar()
	{
		bLigado = true;
		GConfig->GetBool(FofuxoRetargetProps::SecaoIni, FofuxoRetargetProps::ChaveIni, bLigado, GEditorPerProjectIni);

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

	bool EstaLigado() const { return bLigado; }

	/** Quem recebe os editores que o tick encontra. O modulo e o dono. */
	void Avisar(FFofuxoEspelhoDePose* AoEspelho) { Espelho = AoEspelho; }

	void Alternar()
	{
		bLigado = !bLigado;
		GConfig->SetBool(FofuxoRetargetProps::SecaoIni, FofuxoRetargetProps::ChaveIni, bLigado, GEditorPerProjectIni);

		// O tick seguinte pendura ou solta; aqui so se antecipa o soltar para a
		// resposta ser imediata ao clique.
		if (!bLigado)
		{
			for (FEditorAberto& Aberto : Abertos)
			{
				Soltar(Aberto);
			}
		}
	}

private:
	struct FEditorAberto
	{
		TWeakObjectPtr<UIKRetargeter> Asset;

		// De onde os anexos vieram, para saber quando refazer.
		TWeakObjectPtr<UDebugSkelMeshComponent> ComponenteFonte;
		TWeakObjectPtr<UDebugSkelMeshComponent> ComponenteAlvo;
		TWeakObjectPtr<USkeletalMesh> MalhaFonte;
		TWeakObjectPtr<USkeletalMesh> MalhaAlvo;
		bool bEstavaLigado = false;

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

			if (Espelho != nullptr)
			{
				Espelho->Acompanhar(*Editor);
			}
		}

		return true;
	}

	void Sincronizar(FEditorAberto& Aberto, FIKRetargetEditor& Editor)
	{
		const TSharedRef<FIKRetargetEditorController> Controlador = Editor.GetController();

		UDebugSkelMeshComponent* Fonte = Controlador->SourceSkelMeshComponent;
		UDebugSkelMeshComponent* Alvo = Controlador->TargetSkelMeshComponent;

		USkeletalMesh* MalhaFonte = Fonte ? Fonte->GetSkeletalMeshAsset() : nullptr;
		USkeletalMesh* MalhaAlvo = Alvo ? Alvo->GetSkeletalMeshAsset() : nullptr;

		const bool bMudou =
			Aberto.bEstavaLigado != bLigado ||
			Aberto.ComponenteFonte != Fonte ||
			Aberto.ComponenteAlvo != Alvo ||
			Aberto.MalhaFonte != MalhaFonte ||
			Aberto.MalhaAlvo != MalhaAlvo;

		if (!bMudou)
		{
			return;
		}

		Soltar(Aberto);

		if (bLigado)
		{
			PendurarLado(Aberto, Fonte);
			PendurarLado(Aberto, Alvo);
		}

		Aberto.bEstavaLigado = bLigado;
		Aberto.ComponenteFonte = Fonte;
		Aberto.ComponenteAlvo = Alvo;
		Aberto.MalhaFonte = MalhaFonte;
		Aberto.MalhaAlvo = MalhaAlvo;
	}

	void PendurarLado(FEditorAberto& Aberto, UDebugSkelMeshComponent* Componente)
	{
		if (Componente == nullptr)
		{
			return;
		}

		USkeletalMesh* Malha = Componente->GetSkeletalMeshAsset();
		if (Malha == nullptr)
		{
			return;
		}

		UWorld* Mundo = Componente->GetWorld();
		AWorldSettings* Dono = Mundo ? Mundo->GetWorldSettings() : nullptr;
		if (Dono == nullptr)
		{
			return;
		}

		// Primeiro os da malha, depois os do esqueleto -- mesma ordem da Persona.
		FPreviewAssetAttachContainer& DaMalha = Malha->GetPreviewAttachedAssetContainer();
		for (int32 Indice = 0; Indice < DaMalha.Num(); ++Indice)
		{
			Anexar(Aberto, Componente, Dono, DaMalha[Indice].GetAttachedObject(), DaMalha[Indice].AttachedTo);
		}

		if (USkeleton* Esqueleto = Malha->GetSkeleton())
		{
			FPreviewAssetAttachContainer& DoEsqueleto = Esqueleto->PreviewAttachedAssetContainer;
			for (int32 Indice = 0; Indice < DoEsqueleto.Num(); ++Indice)
			{
				Anexar(Aberto, Componente, Dono, DoEsqueleto[Indice].GetAttachedObject(), DoEsqueleto[Indice].AttachedTo);
			}
		}
	}

	void Anexar(
		FEditorAberto& Aberto,
		UDebugSkelMeshComponent* Componente,
		AWorldSettings* Dono,
		UObject* Objeto,
		const FName Encaixe)
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
		// derivados do que ja esta salvo no esqueleto, nao ha o que desfazer nem
		// o que gravar.
		USceneComponent* Anexo = NewObject<USceneComponent>(Dono, Classe, NAME_None, RF_Transient);

		FComponentAssetBrokerage::AssignAssetToComponent(Anexo, Objeto);

		Anexo->SetupAttachment(Componente, Encaixe);
		Anexo->RegisterComponent();

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
	bool bLigado = true;
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

		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FFofuxoRetargetPropsModule::RegistrarMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);

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

		FGerenteDeAnexos* Alvo = Gerente.Get();

		FToolMenuSection& Secao = Barra->FindOrAddSection("Fofuxo");

		Secao.AddEntry(FToolMenuEntry::InitToolBarButton(
			"FofuxoAnexos",
			FUIAction(
				FExecuteAction::CreateLambda([Alvo]() { Alvo->Alternar(); }),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([Alvo]() { return Alvo->EstaLigado(); })),
			LOCTEXT("Anexos", "Anexos"),
			LOCTEXT("AnexosTip",
				"Pendura no visor, na fonte e no alvo, os assets de preview do Skeletal Mesh e do Skeleton "
				"-- os que voce adiciona pelo Add Preview Asset no editor de esqueleto. "
				"Serve para conferir se um osso de arma esta sendo retargetado."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.StaticMesh"),
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
				"No Editing Retarget Pose, repete no osso do outro lado a rotacao que voce der em um osso "
				"-- rodou o thigh_l, o thigh_r acompanha espelhado.\n\n"
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
