// Fofuxo

#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class UFofuxoExportOptions;
class USkeletalMesh;

struct FFofuxoExportPedido
{
	/** Uma animacao vira um take. A ordem aqui e a ordem dos takes no arquivo. */
	TArray<UAnimSequence*> Animacoes;

	USkeletalMesh* SkeletalMesh = nullptr;

	FString CaminhoDoArquivo;

	const UFofuxoExportOptions* Opcoes = nullptr;
};

/**
 * Escreve um FBX com a malha e todas as animacoes, cada uma como um take com o
 * nome do asset.
 *
 * O exportador da engine so sabe fazer um take por documento, e o FbxAnimStack
 * dele e membro privado. O que a gente faz aqui e deixar ele escrever o
 * primeiro take normalmente, pegar a cena pelo no que ele devolve, e acrescentar
 * os outros takes na mao, cada um no seu proprio stack e layer.
 */
/**
 * O caminho de um lote: o mesmo do pedido, com _01, _02 antes da extensao.
 * Total menor ou igual a um devolve o caminho intacto.
 *
 * Com NomeProprio preenchido, ele manda no lugar da numeracao -- e o caso de
 * "uma animacao por arquivo", em que Coisa_37 nao diria nada e o nome da
 * animacao diz tudo.
 */
FString FofuxoCaminhoDoLote(const FString& Base, int32 Indice, int32 Total, const FString& NomeProprio = FString());

class FFofuxoFbxWriter
{
public:
	static bool Exportar(const FFofuxoExportPedido& Pedido, FText& OutErro);
};
