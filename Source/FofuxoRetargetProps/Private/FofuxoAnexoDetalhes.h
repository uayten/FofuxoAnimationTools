// Fofuxo -- o botao Alinhar dentro do op dos anexos

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

/**
 * Poe um botao "Alinhar no mundo" em cada linha da lista de anexos.
 *
 * O motivo de existir e o caminho que a arma faz: voce cadastra o osso e a malha
 * aqui, e ai precisa alinhar aquele osso. Sem isto, alinhar exige sair do painel,
 * achar o osso na hierarquia, seleciona-lo, conferir que o modo armado na barra e
 * o certo, e clicar -- e o painel ja sabe o nome do osso.
 *
 * O botao faz o mesmo que o Alinhar no mundo da barra, com duas diferencas que
 * vem de graca por ele nascer aqui:
 *
 * 1. Nao depende de selecao nenhuma, e por isso funciona fora do Editing Retarget
 *    Pose. O efeito so *aparece* naquele modo, mas a pose e escrita igual.
 *
 * 2. Num anexo em Ambos ele alinha os dois lados numa transacao so, cada um com o
 *    osso que a linha nomeia para aquele lado. E' o caso que a barra nao alcanca:
 *    la a selecao e de um boneco de cada vez.
 *
 * O UIKRetargeter dono da linha e achado pelo endereco: a lista mora dentro do
 * asset, entao o retargeter aberto cujo op contem *este* FFofuxoAnexo e o dono.
 * Comparar ponteiro evita depender do encanamento do painel de detalhes, que
 * envolve wrappers de UObject sem API exportada.
 */
class FFofuxoAnexoDetalhes : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> Criar();

	/** Liga e desliga a customizacao. O modulo e quem chama. */
	static void Registrar();
	static void Esquecer();

	// IPropertyTypeCustomization
	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> Handle,
		FDetailWidgetRow& Linha,
		IPropertyTypeCustomizationUtils& Utilidades) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> Handle,
		IDetailChildrenBuilder& Construtor,
		IPropertyTypeCustomizationUtils& Utilidades) override;
	// End IPropertyTypeCustomization
};
