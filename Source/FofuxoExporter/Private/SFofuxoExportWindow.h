// Fofuxo's Exporter

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class ITableRow;
class SBorder;
class SExpandableArea;
class STableViewBase;
class SWindow;
class UAnimSequence;
class UFofuxoExportOptions;
struct FAssetData;

/** Uma linha da lista de animacoes: a animacao e se ela vai ou nao. */
struct FFofuxoItemDeAnimacao
{
	TWeakObjectPtr<UAnimSequence> Animacao;
	bool bExportar = true;
};

/**
 * A janela do Fofuxo's Export: as opcoes num painel de detalhes, a lista de
 * animacoes com checkbox embaixo, e os botoes.
 *
 * A lista e um SListView e nao um TArray no painel de detalhes porque com uma
 * centena de animacoes uma linha por struct fica impraticavel de marcar.
 */
class SFofuxoExportWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFofuxoExportWindow) {}
		SLATE_ARGUMENT(TSharedPtr<SWindow>, Janela)
		SLATE_ARGUMENT(UFofuxoExportOptions*, Opcoes)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Monta tudo a partir da selecao, abre a janela e exporta se confirmarem. */
	static void Abrir(const TArray<FAssetData>& Selecionados);

	/** As animacoes marcadas, na ordem da lista -- inclusive as que o filtro esconde. */
	TArray<UAnimSequence*> Marcadas() const;

private:
	TSharedRef<ITableRow> GerarLinha(TSharedPtr<FFofuxoItemDeAnimacao> Item, const TSharedRef<STableViewBase>& Tabela);
	FReply MarcarTodas(bool bMarcar);
	FReply AoExportar();
	FReply AoCancelar();
	bool PodeExportar() const;
	FText TextoDoResumo() const;

	/** Refaz a lista visivel a partir do texto da busca. */
	void AplicarFiltro();
	void AoMudarABusca(const FText& Texto);

	// A alca de arrastar embaixo da lista. Enquanto o botao esta preso, cada
	// movimento vira altura; o clamp e para a lista nao sumir nem empurrar os
	// botoes para fora da janela.
	FReply AoPegarAAlca(const FGeometry& Geometria, const FPointerEvent& Evento);
	FReply AoArrastarAAlca(const FGeometry& Geometria, const FPointerEvent& Evento);
	FReply AoSoltarAAlca(const FGeometry& Geometria, const FPointerEvent& Evento);

	TWeakPtr<SWindow> Janela;
	UFofuxoExportOptions* Opcoes = nullptr;

	/** Todas as animacoes. E daqui que sai o que vai para o arquivo. */
	TArray<TSharedPtr<FFofuxoItemDeAnimacao>> Itens;

	/** As que passam pelo filtro. E o que a lista mostra e o que os botoes marcam. */
	TArray<TSharedPtr<FFofuxoItemDeAnimacao>> Visiveis;

	FString Busca;

	TSharedPtr<SListView<TSharedPtr<FFofuxoItemDeAnimacao>>> Lista;
	TSharedPtr<SExpandableArea> AreaDaLista;
	TSharedPtr<SBorder> Alca;

	float AlturaDaLista = 300.f;
	float AlturaAoPegar = 0.f;
	float MouseAoPegar = 0.f;

	bool bConfirmou = false;
};
