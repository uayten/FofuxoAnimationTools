// Fofuxo -- copiar a pose de retarget de outro retargeter

#pragma once

#include "CoreMinimal.h"

class FIKRetargetEditor;
class UToolMenu;
enum class ERetargetSourceOrTarget : uint8;

struct FToolMenuContext;

/**
 * Traz para este retargeter a pose de retarget de outro, casando os ossos pelo
 * nome.
 *
 * O que isto resolve e um conserto que nao viaja. Quando todo retarget do
 * projeto sai do mesmo boneco -- o Manny -- o lado fonte de todos eles tem a
 * mesma pose, e ela foi ajustada uma vez, em um. Nos outros continua torta, e
 * nao ha nada no editor que leve o ajuste de um asset para o outro: e refazer no
 * gizmo, igual, quantas vezes forem os retargeters.
 *
 * Do lado alvo vale o mesmo sempre que os personagens compartilham a convencao
 * de nomes da Unreal e sao posados do mesmo jeito -- que e o caso de um elenco
 * feito pela mesma pessoa.
 *
 * A pose e um mapa de FName para FQuat, e a copia e literal: o delta de
 * "hand_l" la vira o delta de "hand_l" aqui. Nao ha conversao de espaco nem
 * palpite de correspondencia -- osso que nao existir dos dois lados fica de
 * fora, e o numero de ossos de cada categoria aparece na pergunta antes de
 * qualquer escrita.
 *
 * **A pose do destino e substituida, nao misturada.** Copiar quer dizer ficar
 * igual: osso que voce posou aqui e que la nao esta posado volta para o ref
 * pose. Um Ctrl+Z desfaz tudo de uma vez.
 *
 * O deslocamento do pelvis vem junto, e ele *nao* e por nome de osso: e um
 * vetor em centimetros. Entre dois retargets do mesmo Manny e exatamente o que
 * se quer; entre personagens de tamanhos diferentes, e o unico numero desta
 * copia que pode chegar errado.
 */
class FFofuxoCopiarPose
{
public:
	/** Da para copiar agora? Precisa de malha no lado que esta sendo editado. */
	static bool Pode(const FToolMenuContext& Contexto);

	/** O menu do botao: um submenu por retargeter, um item por lado e pose. */
	static void MontarMenu(UToolMenu* Menu);

private:
	/** O editor de retarget dono desta barra, ou nullptr se nao for um. */
	static FIKRetargetEditor* EditorDoContexto(const FToolMenuContext& Contexto);

	/**
	 * Os lados e as poses de um retargeter so.
	 *
	 * Montado sob demanda, quando o mouse para em cima do nome: listar as poses
	 * obriga a carregar o asset, e abrir todos os retargeters do projeto para
	 * desenhar um menu seria caro por nada.
	 */
	static void MontarSubmenuDeUmRetargeter(UToolMenu* Menu, const FSoftObjectPath& Caminho);

	/** O clique num item: confere, pergunta, e substitui a pose do destino. */
	static void Aplicar(
		const FToolMenuContext& Contexto,
		FSoftObjectPath Origem,
		ERetargetSourceOrTarget LadoDaOrigem,
		FName PoseDaOrigem);
};
