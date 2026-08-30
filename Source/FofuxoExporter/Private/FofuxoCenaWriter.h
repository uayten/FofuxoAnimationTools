// Fofuxo

#pragma once

#include "CoreMinimal.h"

struct FFofuxoExportPedido;

/**
 * Os formatos que escrevem uma cena: USD e glTF.
 *
 * Os dois guardam um esqueleto so com varias animacoes penduradas, e os dois
 * sao escritos por modulos separados -- este aqui fatia em lotes, monta o
 * pedido e chama quem estiver ligado. Nenhuma linha de USD ou de glTF passa
 * pelo modulo principal, e e isso que deixa o plugin carregar num projeto que
 * nao tenha esses plugins da engine.
 */
class FFofuxoCenaWriter
{
public:
	static bool Exportar(const FFofuxoExportPedido& Pedido, FText& OutErro);
};
