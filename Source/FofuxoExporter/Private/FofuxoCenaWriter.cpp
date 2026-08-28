// Fofuxo's Exporter

#include "FofuxoCenaWriter.h"

#include "FofuxoCenaUsd.h"
#include "FofuxoExportOptions.h"
#include "FofuxoFbxWriter.h"

#include "Animation/AnimSequence.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "FofuxoExporter"

bool FFofuxoCenaWriter::Exportar(const FFofuxoExportPedido& Pedido, FText& OutErro)
{
	if (Pedido.Opcoes == nullptr)
	{
		OutErro = LOCTEXT("CenaSemOpcoes", "Opcoes de export ausentes.");
		return false;
	}

	// Lista vazia e o pedido de "so a malha". Aqui, ao contrario do FBX, da para
	// desligar a malha -- e sem malha e sem animacao nao sobra arquivo nenhum.
	const bool bSoAMalha = Pedido.Animacoes.Num() == 0;

	if (bSoAMalha && !Pedido.Opcoes->bExportarMalha)
	{
		OutErro = LOCTEXT("CenaSemNada",
			"Sem animacao marcada e com \"Exportar a malha\" desligado nao sobra nada para escrever. "
			"Marque uma animacao, ou ligue a malha.");
		return false;
	}

	const bool bGltf = Pedido.Opcoes->Formato == EFofuxoFormato::GLTF;

	FFofuxoEscreverCena& Escritor = bGltf ? FofuxoEscritorDeCenaGltf() : FofuxoEscritorDeCenaUsd();
	if (!Escritor.IsBound())
	{
		OutErro = bGltf
			? LOCTEXT("GltfSemModulo",
				"O modulo de cena glTF nao carregou. Ligue o plugin \"glTF Exporter\" em Edit > Plugins, "
				"reinicie o editor, e tente de novo.")
			: LOCTEXT("UsdSemModulo",
				"O modulo de cena USD nao carregou. Ligue o plugin \"USD Importer\" em Edit > Plugins, "
				"reinicie o editor, e tente de novo.");
		return false;
	}

	const FString Pasta = FPaths::GetPath(Pedido.CaminhoDoArquivo);
	if (!Pasta.IsEmpty() && !IFileManager::Get().DirectoryExists(*Pasta))
	{
		IFileManager::Get().MakeDirectory(*Pasta, /*Tree*/ true);
	}

	const int32 Total = Pedido.Animacoes.Num();

	// Mesma conta do FBX, mesmo campo: 0 poe tudo num arquivo, e cada lote vira
	// um stage com N animacoes dentro. 1 devolve o comportamento de uma animacao
	// por arquivo. So a malha nao tem o que fatiar: um arquivo, lista vazia.
	const int32 PorArquivo = bSoAMalha
		? 1
		: (Pedido.Opcoes->TakesPorArquivo > 0
			? FMath::Min(Pedido.Opcoes->TakesPorArquivo, Total)
			: Total);

	const int32 NumArquivos = bSoAMalha ? 1 : FMath::DivideAndRoundUp(Total, PorArquivo);

	FScopedSlowTask Progresso(bSoAMalha ? 1 : Total, bGltf
		? LOCTEXT("GltfProgresso", "Fofuxo's Export (glTF)")
		: LOCTEXT("UsdProgresso", "Fofuxo's Export (USD)"));
	Progresso.MakeDialog(/*bShowCancelButton*/ true);

	for (int32 Arquivo = 0; Arquivo < NumArquivos; ++Arquivo)
	{
		const int32 Primeira = Arquivo * PorArquivo;
		const int32 Quantas = FMath::Min(PorArquivo, Total - Primeira);

		FFofuxoPedidoDeCena Cena;
		if (Quantas > 0)
		{
			Cena.Animacoes = TArray<UAnimSequence*>(Pedido.Animacoes.GetData() + Primeira, Quantas);
		}
		Cena.Malha = Pedido.SkeletalMesh;
		// Mesma regra do FBX: uma animacao por arquivo faz o arquivo levar o
		// nome dela.
		const FString NomeProprio = (PorArquivo == 1 && Cena.Animacoes.Num() == 1 && Cena.Animacoes[0] != nullptr)
			? Cena.Animacoes[0]->GetName()
			: FString();

		Cena.Caminho = FofuxoCaminhoDoLote(Pedido.CaminhoDoArquivo, Arquivo, NumArquivos, NomeProprio);

		// O mesmo Destino que manda no FBX manda aqui -- no USD. USD so tem Y e Z
		// para cima; um destino seu em X cai em Z, que e o que a Unreal escreve.
		// O glTF ignora os dois: ele e sempre metros e Y para cima.
		const double Base = (Pedido.Opcoes->Unidade == EFofuxoUnidade::Metros) ? 1.0 : 0.01;
		Cena.MetrosPorUnidade = Base * FMath::Max(Pedido.Opcoes->Escala, UE_KINDA_SMALL_NUMBER);
		Cena.bYParaCima = Pedido.Opcoes->EixoParaCima == EFofuxoEixo::Y;
		Cena.Escala = FMath::Max(Pedido.Opcoes->Escala, UE_KINDA_SMALL_NUMBER);
		Cena.bComMalha = Pedido.Opcoes->bExportarMalha;

		Progresso.EnterProgressFrame(bSoAMalha ? 1.f : Quantas, bSoAMalha
			? FText::Format(
				LOCTEXT("CenaSoAMalha", "Escrevendo {0}"),
				FText::FromString(FPaths::GetCleanFilename(Cena.Caminho)))
			: FText::Format(
				LOCTEXT("CenaEscrevendo", "Escrevendo {0} animacoes em {1}"),
				FText::AsNumber(Quantas),
				FText::FromString(FPaths::GetCleanFilename(Cena.Caminho))));

		// O cancelamento vale entre arquivos: dentro de um stage a escrita e uma
		// chamada so, como no FBX.
		if (Progresso.ShouldCancel())
		{
			OutErro = FText::Format(
				LOCTEXT("CenaCancelou", "Exportacao cancelada. {0} arquivos ja escritos ficaram na pasta."),
				FText::AsNumber(Arquivo));
			return false;
		}

		if (!Escritor.Execute(Cena, OutErro))
		{
			return false;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
