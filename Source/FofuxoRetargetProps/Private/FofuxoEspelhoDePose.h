// Fofuxo -- espelhar a pose de retarget

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class FIKRetargetEditor;
class USkeletalMesh;

/**
 * Repete no osso do outro lado a rotacao que voce acabou de dar em um osso, no
 * Editing Retarget Pose.
 *
 * O editor de retarget nao tem isso: rotacionar o "thigh_l" para consertar a
 * pose deixa o "thigh_r" onde estava, e a unica saida e fazer a conta de cabeca
 * e digitar o oposto no Details. Com o espelho ligado, o outro lado acompanha.
 *
 * Nao ha evento de "a pose mudou" para escutar -- o modo de edicao escreve
 * direto no asset pelo UIKRetargeterController. Entao isto e um vigia: guarda
 * uma copia do mapa de offsets e, todo quadro, compara. Osso que mudou e tem
 * parceiro ganha o espelho; osso cujo parceiro mudou no mesmo quadro fica
 * quieto, que e o caso de voce ter mexido nos dois de proposito (os dois
 * selecionados no gizmo, um Auto Align geral, um Ctrl+Z).
 *
 * As escritas caem dentro da transacao que o gizmo ja abriu, entao um Ctrl+Z
 * desfaz os dois lados de uma vez.
 */
class FFofuxoEspelhoDePose
{
public:
	/** Liga o vigia. O estado do botao vem do ini. */
	void Iniciar();

	/**
	 * Desliga e esquece tudo.
	 *
	 * Obrigatorio no desligamento do modulo: o ticker chama codigo desta DLL, e
	 * um Live Coding com ele ainda registrado derruba o editor.
	 */
	void Encerrar();

	bool EstaLigado() const { return bLigado; }
	void Alternar();

	/**
	 * Poe este editor na lista do vigia, se ainda nao estiver.
	 *
	 * Vem do tick lento do modulo, que ja passeia pelos editores abertos por
	 * causa dos anexos -- descobrir editor de novo aqui seria repetir o passeio.
	 */
	void Acompanhar(FIKRetargetEditor& Editor);

	/**
	 * Os nomes que podem ser o outro lado deste -- so os nomes, sem conferir se
	 * o osso existe.
	 *
	 * Reconhece o lado como segmento separado por "_", ".", "-" ou espaco
	 * (thigh_l, arm.L, L-Hand, "Bip01 L UpperArm"), em qualquer caixa, escrito
	 * l/r, left/right ou lt/rt; e tambem a letra colada em camelCase (HandL,
	 * LHand). Um nome pode dar mais de um candidato -- "L_arm_l" da dois -- e
	 * quem escolhe e o esqueleto.
	 */
	static void NomesEspelhados(const FString& Nome, TArray<FString>& OutCandidatos);

private:
	/** O que o vigia sabe de um editor de retarget aberto. */
	struct FVigiado
	{
		TWeakPtr<class FAssetEditorToolkit> Toolkit;

		// De onde o cache nasceu: mudou qualquer um, o cache se refaz.
		TWeakObjectPtr<USkeletalMesh> Malha;
		uint8 Lado = 0;
		FName Pose;

		// O mapa de offsets como estava no quadro passado.
		TMap<FName, FQuat> Instantaneo;

		// Rotacoes do ref pose em espaco de componente, por indice de osso.
		TArray<FQuat> RefComponente;

		// Osso -> osso do outro lado. Sem parceiro entra como NAME_None, para
		// nao procurar de novo a cada quadro.
		TMap<FName, FName> Parceiros;

		// 0 = X, 1 = Y, 2 = Z: a normal do plano de espelho, deduzida do proprio
		// esqueleto. Personagem da Unreal olha para +X, entao quase sempre e Y.
		int32 Eixo = 1;

		bool bTemCache = false;
	};

	bool Tick(float);
	void Conferir(FVigiado& Vigiado, FIKRetargetEditor& Editor);
	static void RefazerCache(FVigiado& Vigiado, USkeletalMesh* Malha);

	TArray<FVigiado> Vigiados;
	FTSTicker::FDelegateHandle Ticker;
	bool bLigado = false;
};
