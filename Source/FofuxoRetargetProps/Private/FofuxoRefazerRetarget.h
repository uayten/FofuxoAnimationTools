// Fofuxo's Exporter -- refazer o retarget do que ja foi exportado

#pragma once

#include "CoreMinimal.h"

class FIKRetargetEditor;

/**
 * Um "Export Selected Animations" que escolhe as animacoes sozinho: as que este
 * retarget ja produziu uma vez.
 *
 * O trabalho manual que isso poupa e a selecao. Depois de consertar o
 * retargeter, tudo que saiu dele esta errado, e refazer significa achar de novo,
 * na lista da fonte, exatamente as mesmas de antes -- setenta e tantas, no caso
 * do Lizardmen.
 *
 * Quem sabe quais eram e o proprio projeto: o batch retarget duplica a animacao
 * com o mesmo nome, so que no esqueleto do alvo. Entao toda AnimSequence do
 * esqueleto alvo que tenha uma homonima no esqueleto fonte ja passou por aqui.
 *
 * O botao mora na aba Asset Browser, encostado no Export Selected Animations,
 * que e o botao que ele imita. Aquela aba e um widget Slate montado na mao pela
 * IKRigEditor, sem ponto de extensao nenhum -- entao o jeito e enfiar um slot na
 * coluna dela depois que ela existe, e e por isso que isto precisa de alguem
 * chamando GarantirBotao de tempos em tempos.
 */
class FFofuxoRefazerRetarget
{
public:
	/** Poe o botao na aba Asset Browser deste editor, se ainda nao estiver la. */
	static void GarantirBotao(FIKRetargetEditor& Editor);

	/** O clique: junta o que ja foi exportado, pergunta, e refaz. */
	static void AoClicar(FIKRetargetEditor& Editor);

	/**
	 * Tira de volta todos os botoes ja postos.
	 *
	 * Obrigatorio no desligamento do modulo: o widget e as lambdas dele vivem
	 * nesta DLL, e uma aba que continuasse com ele depois do unload -- Live
	 * Coding, por exemplo -- chamaria codigo que nao existe mais.
	 */
	static void Esquecer();
};
