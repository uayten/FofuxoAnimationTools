// Fofuxo -- desenhar e acertar osso no visor do retarget

#pragma once

#include "CoreMinimal.h"

class FIKRetargetEditor;
class FIKRetargetEditorController;
class FPrimitiveDrawInterface;
class FSceneView;
class FViewport;

class HHitProxy;

/**
 * Duas coisas sobre osso no visor: acerta-lo com o mouse, e ve-lo.
 *
 * **Acertar** e o problema de verdade, e nao tem nada a ver com aparencia. A
 * Unreal seleciona osso por hit proxy: cada coisa desenhada e marcada com a
 * identidade de quem a desenhou, e o clique le o pixel debaixo do cursor. Como o
 * osso e desenhado como uma esfera minuscula e uns cones finos, a area clicavel e
 * fina do mesmo jeito -- numa mao, vista de longe, e mirar em um pixel.
 *
 * O Blender nao pede pontaria: ele escolhe o osso mais perto do clique. E' o que
 * OssoPertoDoCursor() faz, e faz no mesmo material com que a Unreal ja trabalha:
 * em vez de ler *um* pixel de hit proxy, le uma caixa de vinte e poucos pixels em
 * volta do cursor e devolve o proxy de osso mais proximo do centro. Clicar perto
 * passa a bastar, e como o que sai dali e um proxy de verdade, quem seleciona
 * continua sendo o modo da propria engine -- painel de detalhes, hierarquia e
 * gizmo se atualizam sozinhos, sem nenhum caminho paralelo.
 *
 * Isso vale sempre, nos dois modos, e nao tem interruptor.
 *
 * **Ver** e o interruptor. O osso da Unreal e desenhado em unidades de mundo:
 * mesmo tamanho para o femur e para a falange, o que na mao vira uma bola cinza
 * e, ao afastar a camera, some. O do Blender em modo *stick* e uma linha fina com
 * um circulo na junta, e -- isto e o que importa -- tem tamanho constante *na
 * tela*: afastar a camera nao engorda nem some.
 *
 * Entao as varetas sao duas coisas juntas:
 *
 * 1. O desenho da engine encolhe, pelo `BoneDrawSize` do proprio retargeter --
 *    o mesmo valor da regua em Character > Bones no visor.
 * 2. Por cima entram linha e circulo de tamanho constante na tela.
 *
 * Encolher em vez de apagar e de proposito: **o hit proxy do osso mora no desenho
 * da engine.** Apagar o desenho apagaria junto a possibilidade de clicar, e nao
 * ha como pendurar a identidade de um osso da IKRigEditor num desenho nosso -- o
 * tipo do proxy dela nao e exportado. Encolhido, ele continua ali, coberto pela
 * vareta, e a busca por proximidade o encontra.
 *
 * Consequencia de o `BoneDrawSize` morar no asset: **salvar o RTG com as varetas
 * ligadas grava o tamanho encolhido.** Desligar devolve o valor e o proximo save
 * conserta; e, se ficar errado, e a mesma regua do Character > Bones.
 */
class FFofuxoOssosNaTela
{
public:
	/** O interruptor das varetas, guardado no ini. */
	static bool EstaLigado();
	static void Alternar();

	/**
	 * Mantem o desenho da engine encolhido enquanto as varetas estao ligadas, e
	 * devolve o tamanho de antes quando elas saem. Vem do passeio de meio segundo.
	 */
	static void Acompanhar(FIKRetargetEditor& Editor);

	/** Devolve o tamanho de antes em todos os retargeters em que mexemos. */
	static void Esquecer();

	/** As varetas dos dois bonecos. Nao faz nada com o interruptor desligado. */
	static void Desenhar(
		const FIKRetargetEditorController& Quem,
		const FSceneView* View,
		FPrimitiveDrawInterface* PDI);

	/**
	 * Um proxy de osso do retargeter dentro do raio de busca, o mais perto do
	 * cursor. nullptr se nao houver nenhum.
	 */
	static HHitProxy* OssoPertoDoCursor(FViewport& Visor, int32 X, int32 Y);

	/** Se este proxy e de osso do retargeter -- pelo nome do tipo, que e o que da. */
	static bool EhDeOsso(HHitProxy* Proxy);
};
