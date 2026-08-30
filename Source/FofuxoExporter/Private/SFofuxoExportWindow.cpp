// Fofuxo

#include "SFofuxoExportWindow.h"
#include "FofuxoNome.h"

#include "FofuxoExportOptions.h"
#include "FofuxoFbxWriter.h"
#include "FofuxoCenaWriter.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformProcess.h"
#include "IDetailsView.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "FofuxoExporter"

void SFofuxoExportWindow::Construct(const FArguments& InArgs)
{
	Janela = InArgs._Janela;
	Opcoes = InArgs._Opcoes;

	if (Opcoes != nullptr && Opcoes->AlturaDaLista > 0.f)
	{
		AlturaDaLista = Opcoes->AlturaDaLista;
	}

	FPropertyEditorModule& EditorDePropriedades =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs Argumentos;
	Argumentos.bAllowSearch = false;
	Argumentos.bShowOptions = false;
	Argumentos.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	const TSharedRef<IDetailsView> Detalhes = EditorDePropriedades.CreateDetailView(Argumentos);
	Detalhes->SetObject(Opcoes);

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(4.f)
		[
			Detalhes
		]

		// Retraida, a lista devolve a altura para o painel de detalhes -- que
		// com a lista aberta fica apertado demais para ver o Avancado inteiro.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.f, 2.f, 8.f, 4.f)
		[
			SAssignNew(AreaDaLista, SExpandableArea)
			.InitiallyCollapsed(Opcoes == nullptr || !Opcoes->bListaExpandida)
			.HeaderContent()
			[
				SNew(STextBlock)
				.Text(this, &SFofuxoExportWindow::TextoDoResumo)
			]
			.BodyContent()
			[
				SNew(SVerticalBox)

				// A alca fica em cima porque e essa a borda que anda: os botoes
				// de Exportar seguram o pe da janela, entao a lista cresce para
				// cima, comendo a altura do painel de detalhes.
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 2.f)
				[
					SAssignNew(Alca, SBorder)
					.BorderImage(FAppStyle::Get().GetBrush("Brushes.Header"))
					.Padding(0.f)
					.Cursor(EMouseCursor::ResizeUpDown)
					.ToolTipText(LOCTEXT("AlcaTip", "Arraste para mudar a altura da lista."))
					.OnMouseButtonDown(this, &SFofuxoExportWindow::AoPegarAAlca)
					.OnMouseMove(this, &SFofuxoExportWindow::AoArrastarAAlca)
					.OnMouseButtonUp(this, &SFofuxoExportWindow::AoSoltarAAlca)
					[
						SNew(SBox)
						.HeightOverride(6.f)
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 2.f, 0.f, 4.f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(SSearchBox)
						.HintText(LOCTEXT("Buscar", "Buscar pelo nome"))
						.ToolTipText(LOCTEXT("BuscarTip",
							"Esconde da lista quem nao casa com o texto. E so a vista: animacao marcada "
							"continua indo para o arquivo mesmo escondida. Marcar tudo e Desmarcar tudo "
							"valem para o que a busca esta mostrando."))
						.OnTextChanged(this, &SFofuxoExportWindow::AoMudarABusca)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("MarcarTudo", "Marcar tudo"))
						.OnClicked(this, &SFofuxoExportWindow::MarcarTodas, true)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("DesmarcarTudo", "Desmarcar tudo"))
						.OnClicked(this, &SFofuxoExportWindow::MarcarTodas, false)
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					// Atributo, e nao valor fixo: assim o arrastar da alca aparece
					// no mesmo quadro, sem remontar widget nenhum.
					.HeightOverride(TAttribute<FOptionalSize>::CreateLambda([this]()
					{
						return FOptionalSize(AlturaDaLista);
					}))
					[
						SAssignNew(Lista, SListView<TSharedPtr<FFofuxoItemDeAnimacao>>)
						.ListItemsSource(&Visiveis)
						.SelectionMode(ESelectionMode::None)
						.OnGenerateRow(this, &SFofuxoExportWindow::GerarLinha)
					]
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.f, 4.f, 8.f, 8.f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("Exportar", "Exportar"))
				.IsEnabled(TAttribute<bool>::CreateSP(this, &SFofuxoExportWindow::PodeExportar))
				.OnClicked(this, &SFofuxoExportWindow::AoExportar)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Cancelar", "Cancelar"))
				.OnClicked(this, &SFofuxoExportWindow::AoCancelar)
			]
		]
	];
}

TSharedRef<ITableRow> SFofuxoExportWindow::GerarLinha(
	TSharedPtr<FFofuxoItemDeAnimacao> Item, const TSharedRef<STableViewBase>& Tabela)
{
	const FString Nome = Item->Animacao.IsValid() ? Item->Animacao->GetName() : FString();

	return SNew(STableRow<TSharedPtr<FFofuxoItemDeAnimacao>>, Tabela)
		.Padding(FMargin(4.f, 2.f))
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([Item]()
			{
				return Item->bExportar ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([Item](ECheckBoxState Estado)
			{
				Item->bExportar = (Estado == ECheckBoxState::Checked);
			})
			[
				SNew(STextBlock)
				.Margin(FMargin(4.f, 0.f, 0.f, 0.f))
				.Text(FText::FromString(Nome))
			]
		];
}

FReply SFofuxoExportWindow::MarcarTodas(bool bMarcar)
{
	// Visiveis, e nao Itens: com uma busca ativa, "tudo" e o que esta a vista --
	// senao o botao desfaria em silencio marcacoes que voce nem esta enxergando.
	for (const TSharedPtr<FFofuxoItemDeAnimacao>& Item : Visiveis)
	{
		Item->bExportar = bMarcar;
	}

	if (Lista.IsValid())
	{
		Lista->RebuildList();
	}

	return FReply::Handled();
}

TArray<UAnimSequence*> SFofuxoExportWindow::Marcadas() const
{
	TArray<UAnimSequence*> Escolhidas;

	for (const TSharedPtr<FFofuxoItemDeAnimacao>& Item : Itens)
	{
		if (Item->bExportar && Item->Animacao.IsValid())
		{
			Escolhidas.Add(Item->Animacao.Get());
		}
	}

	return Escolhidas;
}

FText SFofuxoExportWindow::TextoDoResumo() const
{
	int32 Marcadas = 0;
	for (const TSharedPtr<FFofuxoItemDeAnimacao>& Item : Itens)
	{
		Marcadas += Item->bExportar ? 1 : 0;
	}

	// O resumo diz em quantos arquivos aquilo vai cair, e no formato escolhido.
	// Antes dizia "num FBX so" sempre, o que passou a ser mentira duas vezes:
	// com USD escolhido, e com lote menor que a selecao.
	FText Formato = LOCTEXT("FormatoFbx", "FBX");
	if (Opcoes != nullptr && Opcoes->Formato == EFofuxoFormato::USD)
	{
		Formato = LOCTEXT("FormatoUsd", "USD");
	}
	else if (Opcoes != nullptr && Opcoes->Formato == EFofuxoFormato::GLTF)
	{
		Formato = LOCTEXT("FormatoGltf", "glTF");
	}

	const bool bComMalha = Opcoes == nullptr || Opcoes->bExportarMalha;

	// Nenhuma marcada quer dizer so a malha -- e ai a contagem de animacoes e de
	// arquivos nao diz nada. Sem a malha tambem, nao sobra nada: e o unico estado
	// em que o Exportar fica cinza com malha e pasta escolhidas, entao o resumo e
	// quem explica o porque.
	if (Marcadas == 0)
	{
		return bComMalha
			? FText::Format(
				LOCTEXT("ResumoSoAMalha", "Nenhuma animacao marcada: sai so a malha, num {0} so"),
				Formato)
			: LOCTEXT("ResumoNada",
				"Nenhuma animacao marcada e a malha desligada: nao sobra nada para exportar");
	}

	const int32 PorArquivo = (Opcoes != nullptr && Opcoes->TakesPorArquivo > 0)
		? Opcoes->TakesPorArquivo
		: Marcadas;

	const int32 NumArquivos = (Marcadas > 0 && PorArquivo > 0)
		? FMath::DivideAndRoundUp(Marcadas, PorArquivo)
		: 1;

	const FText Onde = NumArquivos <= 1
		? FText::Format(LOCTEXT("NumArquivoSo", "num {0} so"), Formato)
		: FText::Format(
			LOCTEXT("EmVariosArquivos", "em {0} arquivos {1}"),
			FText::AsNumber(NumArquivos),
			Formato);

	const FText Resumo = bComMalha
		? FText::Format(
			LOCTEXT("Resumo", "{0} de {1} animacoes, {2}"),
			FText::AsNumber(Marcadas),
			FText::AsNumber(Itens.Num()),
			Onde)
		: FText::Format(
			LOCTEXT("ResumoSemMalha", "{0} de {1} animacoes, sem a malha, {2}"),
			FText::AsNumber(Marcadas),
			FText::AsNumber(Itens.Num()),
			Onde);

	if (Busca.IsEmpty())
	{
		return Resumo;
	}

	return FText::Format(
		LOCTEXT("ResumoFiltrado", "{0} -- a busca mostra {1}"),
		Resumo,
		FText::AsNumber(Visiveis.Num()));
}

void SFofuxoExportWindow::AplicarFiltro()
{
	Visiveis.Reset();

	for (const TSharedPtr<FFofuxoItemDeAnimacao>& Item : Itens)
	{
		if (!Item->Animacao.IsValid())
		{
			continue;
		}

		// Contains ja ignora a caixa das letras.
		if (Busca.IsEmpty() || Item->Animacao->GetName().Contains(Busca))
		{
			Visiveis.Add(Item);
		}
	}

	if (Lista.IsValid())
	{
		Lista->RequestListRefresh();
	}
}

void SFofuxoExportWindow::AoMudarABusca(const FText& Texto)
{
	Busca = Texto.ToString().TrimStartAndEnd();
	AplicarFiltro();
}

FReply SFofuxoExportWindow::AoPegarAAlca(const FGeometry& Geometria, const FPointerEvent& Evento)
{
	if (!Alca.IsValid() || Evento.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	AlturaAoPegar = AlturaDaLista;
	MouseAoPegar = static_cast<float>(Evento.GetScreenSpacePosition().Y);

	// Sem capturar o mouse, o ponteiro sai da barra no primeiro movimento um
	// pouco mais rapido e o arrasto morre pela metade.
	return FReply::Handled().CaptureMouse(Alca.ToSharedRef());
}

FReply SFofuxoExportWindow::AoArrastarAAlca(const FGeometry& Geometria, const FPointerEvent& Evento)
{
	if (!Alca.IsValid() || !Alca->HasMouseCapture())
	{
		return FReply::Unhandled();
	}

	const float Andou = static_cast<float>(Evento.GetScreenSpacePosition().Y) - MouseAoPegar;

	// Subtrai: a alca esta no topo, entao arrastar para cima -- Y diminuindo --
	// e o que faz a lista crescer, e a barra fica embaixo do dedo.
	//
	// O teto e para a lista nao empurrar o Exportar para fora da janela; o piso,
	// para sempre sobrar linha visivel.
	AlturaDaLista = FMath::Clamp(AlturaAoPegar - Andou, 80.f, 900.f);

	return FReply::Handled();
}

FReply SFofuxoExportWindow::AoSoltarAAlca(const FGeometry& Geometria, const FPointerEvent& Evento)
{
	if (!Alca.IsValid() || !Alca->HasMouseCapture())
	{
		return FReply::Unhandled();
	}

	return FReply::Handled().ReleaseMouseCapture();
}

bool SFofuxoExportWindow::PodeExportar() const
{
	if (Opcoes == nullptr || Opcoes->SkeletalMesh.IsNull() || Opcoes->Pasta.Path.IsEmpty())
	{
		return false;
	}

	for (const TSharedPtr<FFofuxoItemDeAnimacao>& Item : Itens)
	{
		if (Item->bExportar)
		{
			return true;
		}
	}

	// Nenhuma animacao marcada ainda e um pedido valido: sai a malha sozinha. O
	// que nao da e pedir os dois desligados -- nao sobraria nada no arquivo.
	return Opcoes->bExportarMalha;
}

FReply SFofuxoExportWindow::AoExportar()
{
	bConfirmou = true;
	if (const TSharedPtr<SWindow> Fixa = Janela.Pin())
	{
		Fixa->RequestDestroyWindow();
	}
	return FReply::Handled();
}

FReply SFofuxoExportWindow::AoCancelar()
{
	bConfirmou = false;
	if (const TSharedPtr<SWindow> Fixa = Janela.Pin())
	{
		Fixa->RequestDestroyWindow();
	}
	return FReply::Handled();
}

/**
 * Todas as Animation Sequences do projeto que sao deste esqueleto.
 *
 * Filtra pela tag "Skeleton" do registro de assets, do mesmo jeito que a
 * USkeleton faz -- assim so carrega as que interessam, e nao o projeto inteiro.
 */
static TArray<UAnimSequence*> AnimacoesDoEsqueleto(const USkeleton* Esqueleto)
{
	TArray<UAnimSequence*> Achadas;
	if (Esqueleto == nullptr)
	{
		return Achadas;
	}

	IAssetRegistry& Registro =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> Candidatas;
	Registro.GetAssetsByClass(UAnimSequence::StaticClass()->GetClassPathName(), Candidatas, /*bSearchSubClasses*/ true);

	const FString NomeDoEsqueleto = FAssetData(Esqueleto).GetExportTextName();

	// Filtra primeiro, carrega depois: o GetAsset e que custa caro, e a conta do
	// progresso so faz sentido em cima do que realmente vai ser carregado.
	TArray<FAssetData> Deste;
	for (const FAssetData& Asset : Candidatas)
	{
		if (Asset.GetTagValueRef<FString>(TEXT("Skeleton")) == NomeDoEsqueleto)
		{
			Deste.Add(Asset);
		}
	}

	// Este laco roda antes de a janela existir. Com quatrocentas animacoes por
	// carregar, e aqui que o editor parecia morto antes de qualquer clique.
	FScopedSlowTask Progresso(Deste.Num(), LOCTEXT("Juntando", "Juntando as animacoes do esqueleto"));
	if (Deste.Num() > 8)
	{
		Progresso.MakeDialog();
	}

	for (const FAssetData& Asset : Deste)
	{
		Progresso.EnterProgressFrame(1.f, FText::FromName(Asset.AssetName));

		if (UAnimSequence* Sequencia = Cast<UAnimSequence>(Asset.GetAsset()))
		{
			Achadas.Add(Sequencia);
		}
	}

	return Achadas;
}

void SFofuxoExportWindow::Abrir(const TArray<FAssetData>& Selecionados)
{
	TArray<UAnimSequence*> Animacoes;
	USkeletalMesh* Malha = nullptr;

	for (const FAssetData& Asset : Selecionados)
	{
		if (UObject* Carregado = Asset.GetAsset())
		{
			if (UAnimSequence* Sequencia = Cast<UAnimSequence>(Carregado))
			{
				Animacoes.Add(Sequencia);
			}
			else if (USkeletalMesh* Encontrada = Cast<USkeletalMesh>(Carregado))
			{
				// Se marcarem mais de uma malha, vale a primeira.
				if (Malha == nullptr)
				{
					Malha = Encontrada;
				}
			}
		}
	}

	// Sem malha na selecao, tenta a malha de preview do esqueleto das animacoes.
#if WITH_EDITORONLY_DATA
	if (Malha == nullptr && Animacoes.Num() > 0)
	{
		if (USkeleton* Esqueleto = Animacoes[0]->GetSkeleton())
		{
			Malha = Esqueleto->GetPreviewMesh();
		}
	}
#endif

	// Clicou so na malha: junta todas as animacoes do esqueleto dela. Elas vem
	// marcadas, mas a lista aparece antes de exportar, entao da para desmarcar.
	if (Animacoes.Num() == 0 && Malha != nullptr)
	{
		Animacoes = AnimacoesDoEsqueleto(Malha->GetSkeleton());
	}

	// Sem malha e sem animacao nao ha o que exportar. Com malha e sem animacao ha:
	// a janela abre com a lista vazia e sai um arquivo so com a malha e o
	// esqueleto -- que e o caso de quem so quer a geometria do outro lado.
	if (Animacoes.Num() == 0 && Malha == nullptr)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NadaParaExportar", "Selecione pelo menos uma Animation Sequence ou um Skeletal Mesh."));
		return;
	}

	// Ordem alfabetica para os takes sairem sempre na mesma ordem, e nao na
	// ordem em que voce clicou.
	Animacoes.Sort([](const UAnimSequence& A, const UAnimSequence& B)
	{
		return A.GetName() < B.GetName();
	});

	UFofuxoExportOptions* Opcoes = NewObject<UFofuxoExportOptions>();
	TStrongObjectPtr<UFofuxoExportOptions> Guarda(Opcoes);

	Opcoes->LoadConfig();
	Opcoes->AplicarDestino();
	Opcoes->SkeletalMesh = Malha;

	if (Opcoes->Pasta.Path.IsEmpty())
	{
		Opcoes->Pasta.Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Exportado"));
	}

	Opcoes->NomeDoArquivo = Malha != nullptr ? Malha->GetName() : Animacoes[0]->GetName();

	const TSharedRef<SWindow> NovaJanela = SNew(SWindow)
		.Title(FText::Format(LOCTEXT("TituloDaJanela", "{0} -- Exportar"), Fofuxo::Nome()))
		.ClientSize(FVector2D(600.f, 700.f))
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	const TSharedRef<SFofuxoExportWindow> Conteudo = SNew(SFofuxoExportWindow)
		.Janela(NovaJanela)
		.Opcoes(Opcoes);

	// O que voce desmarcou da ultima vez volta desmarcado. Animacao que nao
	// estava na lista antes entra marcada.
	const TSet<FString> Desmarcadas(Opcoes->Desmarcadas);

	for (UAnimSequence* Sequencia : Animacoes)
	{
		TSharedPtr<FFofuxoItemDeAnimacao> Item = MakeShared<FFofuxoItemDeAnimacao>();
		Item->Animacao = Sequencia;
		Item->bExportar = !Desmarcadas.Contains(Sequencia->GetPathName());
		Conteudo->Itens.Add(Item);
	}

	Conteudo->AplicarFiltro();

	NovaJanela->SetContent(Conteudo);
	FSlateApplication::Get().AddModalWindow(NovaJanela, nullptr);

	if (!Conteudo->bConfirmou)
	{
		return;
	}

	// Guarda as marcacoes sem perder as de outros personagens: mexe so nas
	// animacoes que estavam nesta lista.
	{
		TSet<FString> Atualizadas(Opcoes->Desmarcadas);

		for (const TSharedPtr<FFofuxoItemDeAnimacao>& Item : Conteudo->Itens)
		{
			if (!Item->Animacao.IsValid())
			{
				continue;
			}

			const FString Caminho = Item->Animacao->GetPathName();
			if (Item->bExportar)
			{
				Atualizadas.Remove(Caminho);
			}
			else
			{
				Atualizadas.Add(Caminho);
			}
		}

		Opcoes->Desmarcadas = Atualizadas.Array();
	}

	if (Conteudo->AreaDaLista.IsValid())
	{
		Opcoes->bListaExpandida = Conteudo->AreaDaLista->IsExpanded();
	}

	Opcoes->AlturaDaLista = Conteudo->AlturaDaLista;

	Opcoes->SaveConfig();

	// SaveConfig so marca o ini como sujo -- sem isso ele fica esperando o
	// desligamento limpo do editor, e uma sessao que termine de outro jeito
	// perde a pasta e as marcacoes.
	if (GConfig != nullptr)
	{
		// Sem nome de arquivo ele grava todos os inis sujos. Evita depender de
		// traduzir o nome da classe de config para o caminho do arquivo.
		GConfig->Flush(false);
	}

	FFofuxoExportPedido Pedido;
	Pedido.Animacoes = Conteudo->Marcadas();
	Pedido.SkeletalMesh = Opcoes->SkeletalMesh.LoadSynchronous();
	Pedido.CaminhoDoArquivo = Opcoes->MontarCaminho();
	Pedido.Opcoes = Opcoes;

	const double Comecou = FPlatformTime::Seconds();

	FText Erro;

	const bool bDeuCerto = Opcoes->Formato == EFofuxoFormato::FBX
		? FFofuxoFbxWriter::Exportar(Pedido, Erro)
		: FFofuxoCenaWriter::Exportar(Pedido, Erro);

	if (!bDeuCerto)
	{
		FMessageDialog::Open(EAppMsgType::Ok, Erro);
		return;
	}

	// O tempo no aviso e o que responde "isso e normal ou travou?" da proxima
	// vez: com a barra de progresso a espera deixa de ser cega, e com o numero
	// dela da para comparar.
	const int32 Segundos = FMath::RoundToInt(FPlatformTime::Seconds() - Comecou);

	const FText Tempo = Segundos >= 60
		? FText::Format(LOCTEXT("MinSeg", "{0} min {1} s"), FText::AsNumber(Segundos / 60), FText::AsNumber(Segundos % 60))
		: FText::Format(LOCTEXT("Seg", "{0} s"), FText::AsNumber(Segundos));

	const FText Pasta = FText::FromString(FPaths::GetCleanFilename(FPaths::GetPath(Pedido.CaminhoDoArquivo)));

	FNotificationInfo Aviso(Pedido.Animacoes.Num() == 0
		? FText::Format(
			LOCTEXT("ExportouSoAMalha", "{0} exportada para {1}, em {2}"),
			FText::FromString(Pedido.SkeletalMesh != nullptr ? Pedido.SkeletalMesh->GetName() : FString()),
			Pasta,
			Tempo)
		: FText::Format(
			LOCTEXT("Exportou", "{0} animacoes exportadas para {1}, em {2}"),
			FText::AsNumber(Pedido.Animacoes.Num()),
			Pasta,
			Tempo));

	Aviso.ExpireDuration = 6.f;
	Aviso.bUseSuccessFailIcons = true;

	const FString PastaDoArquivo = FPaths::GetPath(Pedido.CaminhoDoArquivo);
	Aviso.Hyperlink = FSimpleDelegate::CreateLambda([PastaDoArquivo]()
	{
		FPlatformProcess::ExploreFolder(*PastaDoArquivo);
	});
	Aviso.HyperlinkText = LOCTEXT("AbrirPasta", "Abrir a pasta");

	const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Aviso);
	if (Item.IsValid())
	{
		Item->SetCompletionState(SNotificationItem::CS_Success);
	}
}

#undef LOCTEXT_NAMESPACE
