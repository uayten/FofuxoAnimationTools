// Fofuxo -- a pose de retarget como asset

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "FofuxoPoseDeRetarget.generated.h"

/**
 * Uma pose de retarget guardada fora do retargeter, para viajar.
 *
 * O "Copiar pose" resolve dentro de um projeto: ele abre os outros retargeters e
 * pesca a pose de la. Mas o projeto do lado nao esta no asset registry deste, e
 * a pose do Manny -- que e a mesma em todo lugar onde ele e a fonte -- fica
 * presa no projeto onde foi ajustada. Este asset e um arquivo que se copia.
 *
 * **O que esta guardado nao e o delta, e a rotacao local final de cada osso.**
 * A diferenca so aparece quando o esqueleto de destino e outro, que e
 * justamente o caso do MetaHuman:
 *
 *     LocalRot(B) = RefLocal(B).Rot * Delta(B)
 *
 * O delta e a *correcao* medida a partir do ref pose de quem a fez. Levado para
 * um esqueleto cujo ref pose e outro, ele reproduz a correcao, nao a pose --
 * dois A-poses parecidos mas nao iguais chegam em dois lugares diferentes. A
 * rotacao local reproduz a pose: o destino recalcula o delta dele com
 * `Delta = RefLocal.Rot^-1 * Guardada` e cai onde o original caiu. Quando os
 * dois esqueletos sao o mesmo, as duas contas dao exatamente o mesmo numero --
 * entao isto nao e um modo alternativo, e o caso geral.
 *
 * A pose e guardada inteira, osso por osso, e nao so os que estavam posados.
 * Osso nao posado nao quer dizer "deixa como esta": num esqueleto de ref pose
 * diferente, "como esta" e outro lugar. Guardar a pose toda e o que faz o
 * destino chegar na mesma pose, e nao numa mistura das duas.
 *
 * O que continua sendo por nome de osso e a correspondencia. Manny e MetaHuman
 * batem porque seguem a mesma convencao de nomes e de eixos; um esqueleto com
 * outra convencao de eixo de osso nao vai bater, e nenhuma conversao de espaco
 * conserta isso.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Pose de Retarget (Fofuxo)"))
class UFofuxoPoseDeRetarget : public UObject
{
	GENERATED_BODY()

public:
	/** Para voce escrever o que este arquivo e. Nada le isto. */
	UPROPERTY(EditAnywhere, Category = "Anotacao", meta = (MultiLine = true))
	FString Observacao;

	/** A rotacao local de cada osso, no frame do pai. A pose inteira. */
	UPROPERTY(VisibleAnywhere, Category = "Pose")
	TMap<FName, FQuat> RotacoesLocais;

	/** O deslocamento do pelvis, em centimetros. O unico numero de tamanho aqui. */
	UPROPERTY(VisibleAnywhere, Category = "Pose")
	FVector DeslocamentoDoPelvis = FVector::ZeroVector;

	/** Quantos ossos estavam de fato posados quando isto foi salvo. */
	UPROPERTY(VisibleAnywhere, Category = "De onde veio")
	int32 OssosPosados = 0;

	UPROPERTY(VisibleAnywhere, Category = "De onde veio")
	FString Malha;

	UPROPERTY(VisibleAnywhere, Category = "De onde veio")
	FString Esqueleto;

	UPROPERTY(VisibleAnywhere, Category = "De onde veio")
	FString Retargeter;

	UPROPERTY(VisibleAnywhere, Category = "De onde veio")
	FString Lado;

	UPROPERTY(VisibleAnywhere, Category = "De onde veio")
	FString NomeDaPose;

	UPROPERTY(VisibleAnywhere, Category = "De onde veio")
	FString Quando;
};
