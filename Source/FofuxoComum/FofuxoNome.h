// Fofuxo -- o nome do plugin, num lugar so

#pragma once

#include "Internationalization/Text.h"

/**
 * O nome do plugin.
 *
 * Trocar o nome do plugin costuma ser uma cacada por todo o codigo. Aqui nao: as
 * duas linhas abaixo sao a fonte de tudo que o usuario le.
 *
 * **O que estas duas linhas cobrem:** todo texto de interface -- titulo de
 * janela, entrada de menu, barra de progresso, nome do contexto de atalhos.
 *
 * **O que elas nao cobrem, e nao tem como cobrir:**
 *
 * - O `FriendlyName` do `.uplugin`, que e JSON e e lido pela engine antes de
 *   qualquer C++ existir. E' o outro lugar a mudar, e sao dois no total.
 * - O nome da pasta do plugin e os nomes dos modulos (`FofuxoExporter`,
 *   `FofuxoRetargetProps`, ...). Esses sao encanamento: aparecem em caminhos de
 *   arquivo, em `IMPLEMENT_MODULE`, no `.uplugin` e no caminho do repositorio.
 *   Trocar da trabalho e nao muda nada que se veja.
 * - Os cabecalhos dos arquivos, que dizem so `// Fofuxo -- assunto`. Sao
 *   proposital: um comentario que repetisse o nome cheio seria mais um lugar
 *   para esquecer.
 *
 * Este cabecalho fica em `Source/FofuxoComum/`, que nao e modulo -- e uma pasta
 * de include que cada Build.cs interessado poe em `PublicIncludePaths`. Assim
 * nenhum modulo passa a depender de outro so por causa de uma string.
 */
#define FOFUXO_NOME TEXT("Fofuxo's Animation Tools")

/** O mesmo nome onde nao cabe o cheio: entrada de menu, botao, barra. */
#define FOFUXO_NOME_CURTO TEXT("Fofuxo")

namespace Fofuxo
{
	/** O nome cheio, pronto para ir a interface. */
	inline FText Nome()
	{
		return FText::FromString(FOFUXO_NOME);
	}

	/** O nome curto, pronto para ir a interface. */
	inline FText NomeCurto()
	{
		return FText::FromString(FOFUXO_NOME_CURTO);
	}
}
