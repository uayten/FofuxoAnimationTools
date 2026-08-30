// Fofuxo -- Alt+R nos ossos selecionados

#pragma once

#include "CoreMinimal.h"

class FIKRetargetEditor;
class FIKRetargetEditorController;

/**
 * Devolve ao ref pose a rotacao dos ossos selecionados na pose de retarget.
 *
 * E' o mesmo efeito do "Reset Selected Bones" da barra, com duas diferencas que
 * sao o motivo de existir:
 *
 * - **Vale com a animacao rodando.** O comando da engine so pode ser executado no
 *   Editing Retarget Pose, e o Live Retarget existe justamente para ajustar fora
 *   dele. Errar a mao num dedo e ter que sair do modo para desfazer seria perder
 *   o frame em que voce estava.
 * - **Tem tecla.** Alt+R, como no Blender.
 *
 * Nao toca no Deslocamento do op de anexos. Sao coisas de naturezas diferentes --
 * um e rotacao guardada na pose, o outro e translacao somada durante o retarget
 * -- e uma tecla que apagasse os dois nao teria como avisar qual dos dois voce
 * queria de volta.
 */
class FFofuxoZerarRotacao
{
public:
	/** Registra e solta o conjunto de comandos. O modulo e quem chama. */
	static void Registrar();
	static void Esquecer();

	/**
	 * Poe o atalho na lista de comandos deste editor, uma vez so.
	 *
	 * Vai na lista do toolkit, e nao na do visor: assim a tecla vale com o foco em
	 * qualquer painel do editor -- o visor, a hierarquia, a pilha de ops -- que e
	 * onde a selecao de osso pode estar.
	 */
	static void GarantirAtalho(FIKRetargetEditor& Editor);

	static bool Pode(TWeakPtr<FIKRetargetEditorController> Fraco);
	static void Zerar(TWeakPtr<FIKRetargetEditorController> Fraco);
};
