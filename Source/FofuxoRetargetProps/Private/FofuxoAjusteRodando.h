// Fofuxo -- girar osso do alvo com a animacao rodando

#pragma once

#include "CoreMinimal.h"
#include "EdMode.h"

class FIKRetargetEditor;
class FIKRetargetEditorController;

struct FFofuxoAnexosOp;

/**
 * Um gizmo de rotacao no Running Retarget, so no alvo.
 *
 * O que isto resolve e a mao. A pose de retarget se edita olhando o ref pose, e
 * no ref pose a mao esta aberta: nao da para ver se os dedos fecham na espada,
 * que e a unica coisa que importa nos dedos. Voce so descobre que estao errados
 * quando a animacao roda -- e ai o editor de pose ja nao esta mais ligado.
 *
 * **O que o gizmo escreve continua sendo a pose de retarget, e nao um ajuste
 * daquele frame.** O retargeter nao tem onde guardar correcao por frame, e este
 * modo nao inventa um lugar. A conta, porem, faz isso valer a pena:
 *
 * Numa cadeia FK, a saida de um osso e
 *
 *     Saida(B) = DeltaDaFonte(B) * PoseDeRetarget(B)
 *
 * -- o giro que o osso da fonte deu desde a pose de retarget dela, aplicado por
 * cima de onde a pose de retarget do alvo poe o osso. Pos-multiplicar um X na
 * pose de retarget pos-multiplica o mesmo X na saida, em qualquer frame. Entao
 * girar o dedo aqui, olhando o frame 37, escreve o offset que produz *exatamente
 * aquele giro* no frame 37 -- e o mesmo giro, em espaco de mundo, em todos os
 * outros.
 *
 * Para dedo isso e o certo: o erro de um dedo que segura uma espada e constante,
 * e o frame so serve para voce enxerga-lo. Para um erro que muda de frame para
 * frame nao ha o que fazer aqui, e a resposta e uma camada aditiva de Control
 * Rig no Sequencer.
 *
 * A selecao vem de graca: o FIKRetargetDefaultMode, que e quem esta ativo no
 * Running Retarget, ja escolhe osso por clique no visor. Este modo entra junto
 * dele -- os dois convivem porque nenhum dos dois recusa o outro -- e acrescenta
 * so o que faltava, que e o gizmo e o arrasto.
 *
 * **A fonte fica travada** porque o gizmo so aparece com o editor no lado do
 * alvo. Nao ha nada a ajustar na fonte: a animacao dela e o dado de entrada.
 */
class FFofuxoAjusteRodando : public FEdMode
{
public:
	/** O nome com que este modo se registra. */
	static const FEditorModeID Id;

	/** Liga e desliga o registro do modo. O modulo e quem chama. */
	static void Registrar();
	static void Esquecer();

	/** O interruptor da barra, guardado no ini. */
	static bool EstaLigado();
	static void Alternar();

	/**
	 * Poe ou tira o modo neste editor, conforme o interruptor e o modo do
	 * retargeter. Chamado do mesmo passeio de meio segundo que cuida dos anexos.
	 */
	static void Acompanhar(FIKRetargetEditor& Editor);

	/** Fecha o passeio: o aviso de desfazer vale para uma volta so. */
	static void LimparAvisoDeDesfazer();

	void Apontar(const TSharedPtr<FIKRetargetEditorController>& Quem) { Controlador = Quem; }

	// FEdMode
	virtual bool UsesTransformWidget() const override;
	virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const override;
	virtual bool ShouldDrawWidget() const override;
	virtual FVector GetWidgetLocation() const override;
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& OutMatrix, void* InData) override;
	virtual bool GetCustomInputCoordinateSystem(FMatrix& OutMatrix, void* InData) override;
	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport) override;
	virtual bool InputKey(
		FEditorViewportClient* InViewportClient,
		FViewport* InViewport,
		FKey InKey,
		EInputEvent InEvent) override;
	virtual bool InputDelta(
		FEditorViewportClient* InViewportClient,
		FViewport* InViewport,
		FVector& InDrag,
		FRotator& InRot,
		FVector& InScale) override;
	virtual bool HandleClick(
		FEditorViewportClient* InViewportClient,
		HHitProxy* HitProxy,
		const FViewportClick& Click) override;
	virtual void Render(
		const FSceneView* View,
		FViewport* Viewport,
		FPrimitiveDrawInterface* PDI) override;
	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const override { return true; }
	// End FEdMode

private:
	/** O que esta selecionado agora, se e que da para mexer nisso. */
	struct FEscolhido
	{
		FName Osso;
		FTransform NoMundo;
		FQuat DeltaAoIniciar = FQuat::Identity;
	};

	/** A linha do op dos anexos que fala do osso selecionado, se houver uma. */
	struct FLinha
	{
		FFofuxoAnexosOp* Op = nullptr;
		int32 Indice = INDEX_NONE;
		FTransform NoMundo;
		FVector AoIniciar = FVector::ZeroVector;
	};

	/** Os ossos do alvo selecionados, com o estado de cada um no inicio do arrasto. */
	bool Juntar(TArray<FEscolhido>& OutEscolhidos) const;

	/**
	 * Acha a linha do op cujo osso do alvo e o osso selecionado no visor.
	 *
	 * E' assim que o gizmo de mover sabe em qual linha escrever: o painel de
	 * detalhes nao conta para o modo de edicao quem esta selecionado nele, mas o
	 * osso clicado no visor identifica a linha sem canal novo nenhum.
	 */
	bool AcharLinha(FLinha& OutLinha) const;

	TWeakPtr<FIKRetargetEditorController> Controlador;

	TArray<FEscolhido> Arrastando;
	FQuat Acumulado = FQuat::Identity;

	FLinha Movendo;
	FVector AcumuladoDeMover = FVector::ZeroVector;

	bool bEmTransacao = false;
};
