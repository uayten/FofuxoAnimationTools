// Fofuxo -- o painel Transforms do osso, editavel no Live Retarget

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "UObject/GCObject.h"

class UIKRetargetBoneDetails;

/**
 * Substitui o painel "Transforms" do osso selecionado no editor de retarget.
 *
 * O da engine tranca os campos fora do Editing Retarget Pose. A conta que ele faz
 * e uma so, no FIKRetargetBoneDetailCustomization:
 *
 *     bIsEditable = bIsEditingPose && (bIsRelativeOffset || bIsBoneOffset)
 *
 * -- e o `bIsEditingPose` e `GetRetargeterMode() == EditRetargetPose`. Com o Live
 * Retarget ligado isso e falso, entao o gizmo gira o osso no visor e o painel ao
 * lado mostra o resultado em cinza. Sao os dois lados da mesma escrita, e nao
 * havia motivo para um deles estar trancado: o caminho que grava
 * (CommitValueAsRelativeOffset) nao consulta o modo, so escreve na pose de
 * retarget -- exatamente o que o gizmo faz.
 *
 * O que muda aqui, e so isto:
 *
 * - **As quatro linhas existem sempre.** A engine monta ou o par de leitura
 *   (Current, Reference) ou o trio de edicao (Relative Offset, Bone, Reference),
 *   conforme o modo *no instante em que o painel e construido*. Como ligar o Live
 *   Retarget nao reconstroi painel nenhum, uma escolha feita nesse instante ficaria
 *   errada ate voce clicar noutro osso. Com as quatro sempre montadas, o botao que
 *   voce quer esta sempre la.
 * - **O "ligado" e atributo, e nao valor.** Ele e reavaliado a cada quadro, entao
 *   ligar e desligar o Live Retarget destranca e tranca o campo na hora.
 *
 * O resto e o painel da engine: mesmas linhas, mesmos botoes de mundo/local,
 * mesmo copiar e colar, e a escrita passa pelos mesmos metodos do
 * UIKRetargetBoneDetails, que sao exportados.
 *
 * **Location so aparece para edicao no pelvis**, como na engine, e pelo mesmo
 * motivo: quem grava location grava o SetRootTranslationDelta, que e um so para a
 * pose inteira. Deixar o campo aberto num dedo seria oferecer um controle que move
 * outro osso.
 */
class FFofuxoDetalhesDoOsso : public IDetailCustomization, public FGCObject
{
public:
	/**
	 * Poe esta customizacao no lugar da da engine.
	 *
	 * Registrar a mesma classe de novo substitui a anterior no mapa do
	 * PropertyEditor -- e por isso o Esquecer() devolve a da engine, em vez de so
	 * desregistrar: sem isso um Live Coding deixaria o painel do osso cru.
	 */
	static void Registrar();
	static void Esquecer();

	static TSharedRef<IDetailCustomization> MakeInstance();

	// IDetailCustomization
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	// End IDetailCustomization

	// FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
	// End FGCObject

private:
	TArray<TObjectPtr<UIKRetargetBoneDetails>> Ossos;
};
