// Fofuxo

#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class USkeletalMesh;

/** Uma cena a escrever: um esqueleto e varias animacoes, num arquivo so. */
struct FFofuxoPedidoDeCena
{
	TArray<UAnimSequence*> Animacoes;

	/** De onde sai o esqueleto que as animacoes movem. */
	USkeletalMesh* Malha = nullptr;

	FString Caminho;

	/** Quantos metros vale uma unidade. Centimetro e 0,01, o padrao da Unreal. */
	double MetrosPorUnidade = 0.01;

	bool bYParaCima = false;

	/** Multiplica o tamanho. 1 e o tamanho como esta na Unreal. */
	double Escala = 1.0;

	/**
	 * Se a malha vai junto. Desligada, sai so o esqueleto com as animacoes --
	 * bem menor, e o suficiente quando o outro lado ja tem a malha boa. Tambem
	 * evita o custo de converter geometria pesada.
	 */
	bool bComMalha = true;
};

DECLARE_DELEGATE_RetVal_TwoParams(bool, FFofuxoEscreverCena, const FFofuxoPedidoDeCena&, FText&);

using FFofuxoEscreverCenaUsd = FFofuxoEscreverCena;

/**
 * Quem escreve cena USD -- ligado pelo modulo FofuxoUsdCena, que so existe onde
 * o plugin USD da engine esta presente.
 *
 * Este desvio existe para o exportador nao depender de USD em tempo de link. Sem
 * ele, um projeto so de FBX nao carregaria o plugin: faltaria a DLL do USD.
 * Desligado, o delegate fica solto e quem chama devolve uma mensagem.
 */
FOFUXOEXPORTER_API FFofuxoEscreverCena& FofuxoEscritorDeCenaUsd();

/**
 * O mesmo para glTF, ligado pelo modulo FofuxoGltfCena.
 *
 * Sao dois delegates e nao um com parametro de formato porque os modulos sao
 * independentes: quem tem USD pode nao ter glTF, e vice-versa.
 */
FOFUXOEXPORTER_API FFofuxoEscreverCena& FofuxoEscritorDeCenaGltf();
