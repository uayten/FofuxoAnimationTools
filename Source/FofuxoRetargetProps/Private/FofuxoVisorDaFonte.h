// Fofuxo -- uma segunda janela de visor, presa no osso da fonte

#pragma once

#include "CoreMinimal.h"

class FIKRetargetEditor;

/**
 * Uma segunda aba de visor no editor de retarget, olhando o osso da fonte.
 *
 * O problema que ela resolve e o de nao caber tudo numa camera so. Ajustar um
 * dedo do alvo pede a camera colada no dedo; conferir o dedo da fonte, que e o
 * gabarito, pede a camera colada no outro boneco, do outro lado da tela. Com um
 * visor so, cada dedo custa duas viagens de camera, e voce nunca ve os dois ao
 * mesmo tempo.
 *
 * A cena e a *mesma* -- os dois bonecos, a mesma animacao, o mesmo quadro. O que
 * e diferente e so a camera, e por isso os dois visores nunca discordam.
 *
 * O visor novo nao tem gizmo: ele ganha um FEditorModeTools proprio, e os modos
 * do editor de retarget (inclusive o Live Retarget) moram no do toolkit, que e o
 * do visor principal. Isso e de proposito -- dois gizmos para o mesmo osso seriam
 * duas respostas para o mesmo arrasto -- e e o que faz este aqui ser so de olhar
 * e girar a camera.
 *
 * **A camera acompanha o osso a cada quadro.** Nao e um enquadramento de uma vez:
 * se a animacao roda e o personagem anda, e o chao que passa, nao a mao que
 * escapa. A distancia e a orientacao sao as que voce deixou -- o zoom e a orbita
 * continuam seus.
 *
 * O osso da fonte sai da selecao do alvo pelo mapeamento de cadeias: achada a
 * cadeia do alvo que contem o osso clicado, o correspondente e o osso na mesma
 * posicao proporcional da cadeia mapeada. Sem cadeia que sirva, tenta o mesmo
 * nome na fonte; sem isso, o pelvis.
 *
 * A aba nao volta sozinha ao reabrir o editor: o registro dela acontece meio
 * segundo depois de o editor abrir, e o layout salvo ja foi restaurado antes
 * disso. Ela esta em Window, e no botao "Visor da fonte" da barra Fofuxo.
 */
class FFofuxoVisorDaFonte
{
public:
	/** O FTabId desta aba. */
	static const FName IdDaAba;

	/**
	 * Poe o registro da aba neste editor, uma vez so.
	 *
	 * Chamado do mesmo passeio de meio segundo que cuida dos anexos: nao ha evento
	 * de "editor de retarget abriu" para escutar de fora, e o RegisterTabSpawners
	 * do toolkit e da IKRigEditor.
	 */
	static void GarantirAba(FIKRetargetEditor& Editor);

	/** Abre (ou traz para a frente) a aba deste editor. */
	static void Abrir(FIKRetargetEditor& Editor);

	/**
	 * Fecha as abas e solta os registros.
	 *
	 * Sem isto um Live Coding deixaria no ar uma aba cujo widget mora nesta DLL, e
	 * o primeiro quadro depois do unload chamaria codigo que nao existe mais.
	 */
	static void Esquecer();
};
