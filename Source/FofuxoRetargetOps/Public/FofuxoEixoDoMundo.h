// Fofuxo -- o eixo do mundo em que um osso e alinhado

#pragma once

#include "CoreMinimal.h"

#include "FofuxoEixoDoMundo.generated.h"

/**
 * Para onde a ponta do osso aponta ao ser alinhada no mundo.
 *
 * "Ponta do osso" e o X local, que e a convencao do esqueleto da Unreal. Num
 * esqueleto que use outro eixo como comprimento a escolha continua valendo --
 * sao seis orientacoes fixas e repetiveis, e a que parece certa e a que serve --
 * so que ai o nome do item nao descreve para onde o osso vai apontar.
 *
 * Mora no modulo de runtime porque duas coisas leem daqui: o botao Esticar, que
 * e do editor, e o op dos anexos, que e salvo dentro do retargeter.
 */
UENUM(BlueprintType)
enum class EFofuxoEixoDoMundo : uint8
{
	MaisX  UMETA(DisplayName = "+X"),
	MenosX UMETA(DisplayName = "-X"),
	MaisY  UMETA(DisplayName = "+Y"),
	MenosY UMETA(DisplayName = "-Y"),
	MaisZ  UMETA(DisplayName = "+Z"),
	MenosZ UMETA(DisplayName = "-Z"),
};
