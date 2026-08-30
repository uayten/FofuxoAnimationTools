// Fofuxo

#include "FofuxoCenaUsd.h"

FFofuxoEscreverCena& FofuxoEscritorDeCenaUsd()
{
	// Estatico de funcao: o modulo que liga isto carrega depois deste, e uma
	// variavel global de arquivo poderia nao estar construida a tempo.
	static FFofuxoEscreverCena Escritor;
	return Escritor;
}

FFofuxoEscreverCena& FofuxoEscritorDeCenaGltf()
{
	static FFofuxoEscreverCena Escritor;
	return Escritor;
}
