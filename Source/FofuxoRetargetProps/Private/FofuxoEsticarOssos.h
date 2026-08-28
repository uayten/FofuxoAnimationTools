// Fofuxo's Exporter -- esticar ossos na pose de retarget

#pragma once

#include "CoreMinimal.h"
#include "Textures/SlateIcon.h"

class FIKRetargetEditor;
class UToolMenu;

struct FToolMenuContext;

/** O que o botao Esticar faz ao ser clicado. Vive no ini, entre sessoes. */
enum class EFofuxoModoDeEsticar : uint8
{
	/** So os ossos selecionados. */
	Selecionados,

	/** Os selecionados e tudo que desce deles. */
	ComFilhos,

	/** A selecao inteira ganha a orientacao que o ultimo osso clicado tinha. */
	NoUltimo,

	/** Os eixos de cada osso selecionado caem em cima dos eixos do mundo. */
	NoMundo,
};

/**
 * Para onde a ponta do osso aponta no modo NoMundo. Tambem vive no ini.
 *
 * "Ponta do osso" e o X local, que e a convencao do esqueleto da Unreal. Num
 * esqueleto que use outro eixo como comprimento a escolha continua valendo --
 * sao seis orientacoes fixas e repetiveis, e a que parece certa e a que serve --
 * so que ai o nome do item nao descreve para onde o osso vai apontar.
 */
enum class EFofuxoEixoDoMundo : uint8
{
	MaisX,
	MenosX,
	MaisY,
	MenosY,
	MaisZ,
	MenosZ,
};

/**
 * Endireita a pose de retarget de um jeito que o gizmo nao alcanca.
 *
 * Tres problemas diferentes, e por isso o botao tem modos.
 *
 * O primeiro e o dedo: alinhar falange por falange no olho e trabalho de
 * precisao que quase nunca fica reto de verdade -- so que "reto" aqui tem
 * definicao exata, que e a rotacao local do osso ser identidade. A pose de
 * retarget guarda, por osso, um delta local pos-multiplicado no ref pose:
 *
 *     LocalRot(B) = RefLocal(B).Rot * Delta(B)
 *
 * Entao "o filho com os eixos do pai" nao e conta de geometria, e sim uma
 * equacao de uma linha: LocalRot tem que dar identidade, logo
 * Delta = RefLocal.Rot^-1. Nao depende da pose do pai nem do resto da cadeia,
 * e por isso a ordem em que os ossos sao esticados nao importa. E' o Alt+R do
 * Blender, e sao os modos Selecionados e ComFilhos.
 *
 * O que isso *nao* faz e endireitar a posicao: o osso continua no deslocamento
 * que o ref pose deu a ele. Numa mao normal, em que as falanges apontam todas
 * para o mesmo eixo local, orientacao igual ja da dedo reto -- que e o caso de
 * uso. Num esqueleto em que as falanges nascem tortas entre si, sobra a torcao
 * que estava na malha desde o comeco, e nenhuma rotacao tira aquilo.
 *
 * O segundo problema e a cadeia que ja esta certa em um osso e errada nos
 * outros: voce acertou a ultima falange no gizmo e quer as duas de tras iguais
 * aquela. Ai o alvo nao e o pai de cada um, e sim uma orientacao unica, medida
 * em espaco de componente. Como o osso corrigido pode ser filho de outro osso
 * corrigido, isto *depende* da ordem, e a conta sai numa passada da raiz para
 * as folhas:
 *
 *     Delta(B) = RefLocal(B).Rot^-1 * CS(pai de B)^-1 * Alvo
 *
 * com CS(pai) ja recalculado. E' o modo NoUltimo. O proprio osso de referencia
 * entra na conta: se um ancestral dele foi virado, ele saiu do lugar junto e
 * precisa voltar -- no fim, a selecao inteira aponta para o mesmo lado.
 *
 * O terceiro e a arma. Um osso de arma so presta se estiver na *mesma*
 * orientacao no personagem e na arma, e "mesma" precisa de uma referencia que
 * nao seja nenhum dos dois, senao cada asset e ajustado contra o outro e nada
 * fecha. Essa referencia e o mundo: o modo NoMundo e o NoUltimo com o Alvo
 * fixado numa constante, e ai o osso fica com os proprios eixos em cima dos
 * eixos do mundo. E' o osso deitado apontando para +Y do Blender -- la o osso e
 * desenhado ao longo do proprio Y, aqui a convencao da Unreal poe o
 * comprimento no X. O estado e o mesmo; muda so por onde cada programa desenha
 * o osso.
 *
 * A constante nao e sempre a identidade porque nem todo mundo quer a arma
 * apontando para o mesmo lado: EFofuxoEixoDoMundo escolhe entre seis, e a
 * identidade e a de +X. *Qual* delas nao muda nada, desde que seja a mesma nos
 * dois assets -- o que importa e ser constante, e nao medida tirada de um deles.
 *
 * Alinhados assim, os dois batem sem ninguem medir nada, e batem no Running
 * Retarget, que e onde interessa.
 */
class FFofuxoEsticarOssos
{
public:
	/** O modo armado. Lido do ini na primeira chamada. */
	static EFofuxoModoDeEsticar Modo();

	/** Arma outro modo e grava no ini. */
	static void EscolherModo(EFofuxoModoDeEsticar Novo);

	/** O eixo escolhido para o modo NoMundo. Lido do ini na primeira chamada. */
	static EFofuxoEixoDoMundo Eixo();

	/** Escolhe o eixo, grava no ini, e arma o NoMundo junto -- e o unico modo que o usa. */
	static void EscolherEixo(EFofuxoEixoDoMundo Novo);

	/** "+X", "-Y"... */
	static FText NomeDoEixo(EFofuxoEixoDoMundo Doque);

	/**
	 * O rotulo do botao -- muda com o modo, que e como se ve qual esta armado.
	 *
	 * No NoMundo o eixo entra no rotulo: sem isso a barra nao diria para onde o
	 * clique vai apontar o osso, e seriam seis coisas diferentes com o mesmo nome.
	 */
	static FText Rotulo(EFofuxoModoDeEsticar Doque);
	static FText Dica(EFofuxoModoDeEsticar Doque);
	static FSlateIcon Icone(EFofuxoModoDeEsticar Doque);

	/** Estamos no Editing Retarget Pose, com ossos suficientes selecionados? */
	static bool Pode(const FToolMenuContext& Contexto);

	/** O clique do botao: faz o que o modo armado manda. */
	static void Esticar(const FToolMenuContext& Contexto);

	/** O menuzinho dos tres pontos, que e onde se troca de modo. */
	static void MontarMenuDeModos(UToolMenu* Menu);

private:
	/** O editor de retarget dono desta barra, ou nullptr se nao for um. */
	static FIKRetargetEditor* EditorDoContexto(const FToolMenuContext& Contexto);
};
