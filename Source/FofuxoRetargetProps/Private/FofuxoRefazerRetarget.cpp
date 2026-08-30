// Fofuxo -- refazer o retarget do que ja foi exportado

#include "FofuxoRefazerRetarget.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "RetargetEditor/IKRetargetBatchOperation.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"
#include "SPositiveActionButton.h"
#include "Styling/AppStyle.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoRetarget, Log, All);

namespace FofuxoRefazer
{
	/**
	 * O editor por tras de um ponteiro fraco de toolkit.
	 *
	 * A confirmacao do nome nao e paranoia: o botao vive dentro da aba, e a aba
	 * sobrevive a mudancas de modo do editor.
	 */
	static FIKRetargetEditor* EditorDoToolkit(const TWeakPtr<FAssetEditorToolkit>& Fraco)
	{
		const TSharedPtr<FAssetEditorToolkit> Toolkit = Fraco.Pin();
		if (!Toolkit.IsValid() || Toolkit->GetEditorName() != FName("IKRetargetEditor"))
		{
			return nullptr;
		}

		return static_cast<FIKRetargetEditor*>(Toolkit.Get());
	}

	/** O primeiro widget deste tipo, descendo a arvore. */
	static TSharedPtr<SWidget> Procurar(const TSharedRef<SWidget>& Raiz, const FName Tipo)
	{
		if (Raiz->GetType() == Tipo)
		{
			return Raiz;
		}

		FChildren* Filhos = Raiz->GetChildren();
		for (int32 Indice = 0; Filhos != nullptr && Indice < Filhos->Num(); ++Indice)
		{
			if (const TSharedPtr<SWidget> Achado = Procurar(Filhos->GetChildAt(Indice), Tipo))
			{
				return Achado;
			}
		}

		return nullptr;
	}

	/**
	 * A coluna da aba Asset Browser -- aquela com o Export Selected Animations em
	 * cima, a lista no meio e o Play Ref Pose embaixo.
	 */
	static TSharedPtr<SVerticalBox> ColunaDoAssetBrowser(FIKRetargetEditor& Editor)
	{
		const TSharedPtr<FTabManager> Abas = Editor.GetTabManager();
		if (!Abas.IsValid())
		{
			return nullptr;
		}

		// O valor de FIKRetargetAssetBrowserTabSummoner::TabID. A struct nao tem
		// macro de API, entao o simbolo nao linka de fora da IKRigEditor e o nome
		// vem escrito aqui.
		const TSharedPtr<SDockTab> Aba = Abas->FindExistingLiveTab(FTabId(TEXT("AssetBrowser")));
		if (!Aba.IsValid())
		{
			return nullptr;
		}

		const TSharedPtr<SWidget> Browser = Procurar(Aba->GetContent(), TEXT("SIKRetargetAssetBrowser"));
		if (!Browser.IsValid())
		{
			return nullptr;
		}

		const TSharedPtr<SWidget> Coluna = Procurar(Browser.ToSharedRef(), TEXT("SVerticalBox"));
		if (!Coluna.IsValid())
		{
			return nullptr;
		}

		return StaticCastSharedPtr<SVerticalBox>(Coluna);
	}

	/** Um botao ja posto, para nao repetir e para saber o que tirar depois. */
	struct FPosto
	{
		TWeakPtr<SVerticalBox> Coluna;
		TWeakPtr<SWidget> Botao;
	};

	static TArray<FPosto> Postos;

	/** As tres pecas do retarget que o batch precisa. */
	struct FLados
	{
		USkeletalMesh* MalhaFonte = nullptr;
		USkeletalMesh* MalhaAlvo = nullptr;
		UIKRetargeter* Retargeter = nullptr;

		bool Servem() const
		{
			return MalhaFonte != nullptr
				&& MalhaAlvo != nullptr
				&& Retargeter != nullptr
				&& MalhaFonte->GetSkeleton() != nullptr
				&& MalhaAlvo->GetSkeleton() != nullptr
				&& MalhaFonte->GetSkeleton() != MalhaAlvo->GetSkeleton();
		}
	};

	static FLados LadosDoEditor(FIKRetargetEditor& Editor)
	{
		const TSharedRef<FIKRetargetEditorController> Controlador = Editor.GetController();

		FLados Lados;

		// Pelo AssetController, e nao pelo FIKRetargetEditorController::GetSkeletalMesh,
		// que faz exatamente isto uma linha adiante: aquele metodo nao tem
		// IKRIGEDITOR_API e nao linka de fora do modulo.
		if (UIKRetargeterController* Dono = Controlador->AssetController)
		{
			Lados.MalhaFonte = Dono->GetPreviewMesh(ERetargetSourceOrTarget::Source);
			Lados.MalhaAlvo = Dono->GetPreviewMesh(ERetargetSourceOrTarget::Target);
			Lados.Retargeter = Dono->GetAsset();
		}

		return Lados;
	}

	/**
	 * As animacoes ja exportadas, agrupadas pela pasta onde estao -- e o que vai
	 * em cada grupo e a *fonte* de cada uma, que e o que o batch retarget come.
	 *
	 * Uma passada so no registro de assets, sem carregar nada: o esqueleto de uma
	 * AnimSequence esta na tag "Skeleton", e o nome esta no proprio FAssetData.
	 */
	static void JuntarPorPasta(
		const FLados& Lados,
		TMap<FString, TArray<FAssetData>>& OutPorPasta,
		TArray<FString>& OutSemFonte)
	{
		IAssetRegistry& Registro =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		TArray<FAssetData> Todas;
		Registro.GetAssetsByClass(UAnimSequence::StaticClass()->GetClassPathName(), Todas, /*bSearchSubClasses*/ true);

		const FString EsqueletoFonte = FAssetData(Lados.MalhaFonte->GetSkeleton()).GetExportTextName();
		const FString EsqueletoAlvo = FAssetData(Lados.MalhaAlvo->GetSkeleton()).GetExportTextName();

		TMap<FString, FAssetData> Fontes;
		TArray<FAssetData> Exportadas;

		for (const FAssetData& Asset : Todas)
		{
			const FString Esqueleto = Asset.GetTagValueRef<FString>(TEXT("Skeleton"));
			const FString Nome = Asset.AssetName.ToString();

			if (Esqueleto == EsqueletoFonte)
			{
				// Duas fontes homonimas em pastas diferentes: fica a primeira, e a
				// outra aparece no log -- adivinhar qual das duas gerou o arquivo
				// nao da, e escolher em silencio seria pior.
				if (const FAssetData* Antes = Fontes.Find(Nome))
				{
					UE_LOG(LogFofuxoRetarget, Warning,
						TEXT("Duas animacoes fonte chamadas \"%s\": fico com %s e ignoro %s."),
						*Nome, *Antes->PackageName.ToString(), *Asset.PackageName.ToString());
					continue;
				}

				Fontes.Add(Nome, Asset);
			}
			else if (Esqueleto == EsqueletoAlvo)
			{
				Exportadas.Add(Asset);
			}
		}

		for (const FAssetData& Exportada : Exportadas)
		{
			const FString Nome = Exportada.AssetName.ToString();

			if (const FAssetData* Fonte = Fontes.Find(Nome))
			{
				const FString Pasta = FPaths::GetPath(Exportada.PackageName.ToString());
				OutPorPasta.FindOrAdd(Pasta).Add(*Fonte);
			}
			else
			{
				// Animacao do esqueleto alvo sem homonima na fonte: feita a mao,
				// ou exportada com prefixo/sufixo. Nao e nossa, fica quieta.
				OutSemFonte.Add(Nome);
			}
		}
	}

	static FText MontarPergunta(
		const TMap<FString, TArray<FAssetData>>& PorPasta,
		const TArray<FString>& SemFonte,
		const FLados& Lados)
	{
		int32 Quantas = 0;
		FString Pastas;

		for (const TPair<FString, TArray<FAssetData>>& Par : PorPasta)
		{
			Quantas += Par.Value.Num();
			Pastas += FString::Printf(TEXT("\n    %s  (%d)"), *Par.Key, Par.Value.Num());
		}

		FText Texto = FText::Format(
			LOCTEXT("Pergunta",
				"Achei {0} animacoes que ja saíram deste retarget:\n{1}\n\n"
				"Vou refazer todas com o {2} e sobrescrever os assets que estao la. Continuar?"),
			FText::AsNumber(Quantas),
			FText::FromString(Pastas),
			FText::FromString(Lados.Retargeter->GetName()));

		if (SemFonte.Num() > 0)
		{
			Texto = FText::Format(
				LOCTEXT("PerguntaComSobra",
					"{0}\n\n({1} animacoes do esqueleto de {2} nao tem fonte de mesmo nome e ficam como estao. "
					"Os nomes estao no Output Log.)"),
				Texto,
				FText::AsNumber(SemFonte.Num()),
				FText::FromString(Lados.MalhaAlvo->GetName()));
		}

		return Texto;
	}
}

void FFofuxoRefazerRetarget::GarantirBotao(FIKRetargetEditor& Editor)
{
	const TSharedPtr<SVerticalBox> Coluna = FofuxoRefazer::ColunaDoAssetBrowser(Editor);
	if (!Coluna.IsValid())
	{
		// Aba fechada, ou o editor ainda montando. Volta no proximo tick.
		return;
	}

	FofuxoRefazer::Postos.RemoveAll([](const FofuxoRefazer::FPosto& Posto)
	{
		return !Posto.Coluna.IsValid() || !Posto.Botao.IsValid();
	});

	for (const FofuxoRefazer::FPosto& Posto : FofuxoRefazer::Postos)
	{
		if (Posto.Coluna.Pin() == Coluna)
		{
			return;
		}
	}

	// So o toolkit fica guardado: o editor concreto sai dele na hora do clique, e
	// um ponteiro cru sobreviveria ao fechamento da janela.
	const TWeakPtr<FAssetEditorToolkit> Fraco = StaticCastSharedRef<FAssetEditorToolkit>(Editor.AsShared());

	const TSharedRef<SWidget> Botao = SNew(SPositiveActionButton)
		.Icon(FAppStyle::Get().GetBrush("Icons.Refresh"))
		.Text(LOCTEXT("Refazer", "Refazer as ja exportadas"))
		.ToolTipText(LOCTEXT("RefazerTip",
			"O mesmo que o botao de cima, so que escolhendo as animacoes sozinho: as que este retarget "
			"ja produziu uma vez. Serve para depois de mexer no retargeter, sem ter que achar e marcar "
			"todas na lista outra vez.\n\n"
			"Reconhece pelo nome -- toda animacao do esqueleto alvo que tenha uma homonima no esqueleto "
			"fonte ja saiu daqui. Elas sao refeitas por cima, cada uma na pasta onde ja esta."))
		.IsEnabled(TAttribute<bool>::CreateLambda([Fraco]()
		{
			FIKRetargetEditor* Aberto = FofuxoRefazer::EditorDoToolkit(Fraco);
			return Aberto != nullptr && FofuxoRefazer::LadosDoEditor(*Aberto).Servem();
		}))
		.OnClicked(FOnClicked::CreateLambda([Fraco]()
		{
			if (FIKRetargetEditor* Aberto = FofuxoRefazer::EditorDoToolkit(Fraco))
			{
				FFofuxoRefazerRetarget::AoClicar(*Aberto);
			}

			return FReply::Handled();
		}));

	// Slot 1: logo abaixo do Export Selected Animations, antes da lista.
	Coluna->InsertSlot(1)
		.AutoHeight()
		[
			Botao
		];

	FofuxoRefazer::FPosto Novo;
	Novo.Coluna = Coluna;
	Novo.Botao = Botao;
	FofuxoRefazer::Postos.Add(Novo);
}

void FFofuxoRefazerRetarget::Esquecer()
{
	for (const FofuxoRefazer::FPosto& Posto : FofuxoRefazer::Postos)
	{
		const TSharedPtr<SVerticalBox> Coluna = Posto.Coluna.Pin();
		const TSharedPtr<SWidget> Botao = Posto.Botao.Pin();

		if (Coluna.IsValid() && Botao.IsValid())
		{
			Coluna->RemoveSlot(Botao.ToSharedRef());
		}
	}

	FofuxoRefazer::Postos.Reset();
}

void FFofuxoRefazerRetarget::AoClicar(FIKRetargetEditor& Editor)
{
	const FofuxoRefazer::FLados Lados = FofuxoRefazer::LadosDoEditor(Editor);
	if (!Lados.Servem())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("SemLados", "Este retargeter precisa de uma malha na fonte e outra no alvo, de esqueletos diferentes."));
		return;
	}

	TMap<FString, TArray<FAssetData>> PorPasta;
	TArray<FString> SemFonte;
	FofuxoRefazer::JuntarPorPasta(Lados, PorPasta, SemFonte);

	for (const FString& Nome : SemFonte)
	{
		UE_LOG(LogFofuxoRetarget, Display,
			TEXT("\"%s\" e do esqueleto alvo mas nao tem fonte de mesmo nome -- fora do refazer."), *Nome);
	}

	if (PorPasta.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("NadaParaRefazer",
				"Nao achei nenhuma animacao no esqueleto de {0} que tenha uma fonte de mesmo nome no esqueleto de {1}.\n\n"
				"E o que este botao procura: o batch retarget duplica mantendo o nome, entao e assim que da para "
				"reconhecer o que ja passou por aqui. Se voce exportou com prefixo ou sufixo, o nome nao bate mais."),
			FText::FromString(Lados.MalhaAlvo->GetName()),
			FText::FromString(Lados.MalhaFonte->GetName())));

		return;
	}

	const EAppReturnType::Type Resposta = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FofuxoRefazer::MontarPergunta(PorPasta, SemFonte, Lados),
		LOCTEXT("TituloRefazer", "Refazer as ja exportadas"));

	if (Resposta != EAppReturnType::Yes)
	{
		return;
	}

	// Uma rodada por pasta: o batch escreve tudo num destino so, entao animacoes
	// que moram em pastas diferentes tem que ir em levas separadas para cada uma
	// voltar para o lugar de onde veio.
	int32 Refeitas = 0;

	for (const TPair<FString, TArray<FAssetData>>& Par : PorPasta)
	{
		FIKRetargetBatchOperationInputs Entrada;
		Entrada.AssetsToRetarget = Par.Value;
		Entrada.SourceMesh = Lados.MalhaFonte;
		Entrada.TargetMesh = Lados.MalhaAlvo;
		Entrada.IKRetargetAsset = Lados.Retargeter;
		Entrada.InOverrideSetNames = Lados.Retargeter->GetOverrideSetsToApply();
		Entrada.TargetPath = Par.Key;
		Entrada.bUseSourcePath = false;

		// Nome igual ao que ja esta la, e por cima: e justamente o ponto. Sem
		// isto o batch criaria AS_Coisa1, AS_Coisa2, e quem usa a animacao
		// continuaria apontando para a versao errada.
		Entrada.bOverwriteExistingFiles = true;

		// So o que foi pedido. Puxar os referenciados traria montages e
		// blendspaces que ninguem exportou daqui.
		Entrada.bIncludeReferencedAssets = false;

		UE_LOG(LogFofuxoRetarget, Display,
			TEXT("Refazendo %d animacoes em %s"), Par.Value.Num(), *Par.Key);

		Refeitas += UIKRetargetBatchOperation::RunBatchRetarget(Entrada).Num();
	}

	UE_LOG(LogFofuxoRetarget, Display, TEXT("Refazer terminou: %d animacoes."), Refeitas);
}

#undef LOCTEXT_NAMESPACE
