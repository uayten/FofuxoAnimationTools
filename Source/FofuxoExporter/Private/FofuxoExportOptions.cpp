// Fofuxo

#include "FofuxoExportOptions.h"

#include "Misc/Paths.h"

const TCHAR* UFofuxoExportOptions::DestinoBlender = TEXT("Blender");
const TCHAR* UFofuxoExportOptions::DestinoUnity = TEXT("Unity");

TArray<FString> UFofuxoExportOptions::ObterNomesDeDestino() const
{
	TArray<FString> Nomes;
	Nomes.Add(DestinoBlender);
	Nomes.Add(DestinoUnity);

	for (const FFofuxoDestino& Meu : MeusDestinos)
	{
		// Um destino sem nome nao teria como ser escolhido de volta, e um nome
		// repetido apontaria para dois lugares.
		if (!Meu.Nome.IsEmpty() && !Nomes.Contains(Meu.Nome))
		{
			Nomes.Add(Meu.Nome);
		}
	}

	return Nomes;
}

void UFofuxoExportOptions::AplicarDestino()
{
	if (Destino.IsEmpty())
	{
		Destino = DestinoBlender;
	}

	for (const FFofuxoDestino& Meu : MeusDestinos)
	{
		if (Meu.Nome == Destino)
		{
			EixoParaCima = Meu.EixoParaCima;
			bFrenteNoEixoX = Meu.bFrenteNoEixoX;
			Unidade = Meu.Unidade;
			Escala = Meu.Escala;
			bDestinoEhMeu = true;
			return;
		}
	}

	bDestinoEhMeu = false;

	if (Destino == DestinoUnity)
	{
		// A Unity converte a unidade sozinha na importacao, mas nao o eixo.
		EixoParaCima = EFofuxoEixo::Y;
		bFrenteNoEixoX = false;
		Unidade = EFofuxoUnidade::Centimetros;
		Escala = 1.f;
		return;
	}

	// Blender, e tambem o caso de um nome que nao existe mais.
	//
	// Centimetros de volta. Metros pareciam a saida para o objeto entrar com
	// escala 1 no Blender, mas nao sao: o ConvertScene do FBX SDK nao mexe nos
	// vertices, ele poe a escala nos nos -- entao o 0.01 aparecia no objeto do
	// mesmo jeito, so que agora vindo de dentro do arquivo. Com centimetros o
	// arquivo sai identico ao export nativo da Unreal, e quem decide a escala do
	// objeto e o importador.
	Destino = DestinoBlender;
	EixoParaCima = EFofuxoEixo::Z;
	bFrenteNoEixoX = false;
	Unidade = EFofuxoUnidade::Centimetros;
	Escala = 1.f;
}

void UFofuxoExportOptions::GravarNoDestino()
{
	if (!bDestinoEhMeu)
	{
		return;
	}

	for (FFofuxoDestino& Meu : MeusDestinos)
	{
		if (Meu.Nome == Destino)
		{
			Meu.EixoParaCima = EixoParaCima;
			Meu.bFrenteNoEixoX = bFrenteNoEixoX;
			Meu.Unidade = Unidade;
			Meu.Escala = Escala;
			return;
		}
	}
}

FString UFofuxoExportOptions::Extensao() const
{
	if (Formato == EFofuxoFormato::USD)
	{
		// Em USD quem decide texto ou binario e a extensao, e nao uma opcao do
		// exportador: .usda e texto, .usd cai no crate binario.
		return bASCII ? TEXT(".usda") : TEXT(".usd");
	}

	if (Formato == EFofuxoFormato::GLTF)
	{
		// Mesma ideia: .glb e um arquivo binario unico, .gltf e um JSON legivel
		// com os dados num .bin ao lado.
		return bASCII ? TEXT(".gltf") : TEXT(".glb");
	}

	return TEXT(".fbx");
}

FString UFofuxoExportOptions::MontarCaminho(int32 Indice, int32 Total) const
{
	FString Nome = NomeDoArquivo;
	if (Nome.IsEmpty())
	{
		Nome = TEXT("Exportado");
	}

	// Tira a extensao que a pessoa por acaso tenha digitado: senao trocar de
	// formato produz "Coisa.fbx.usd", e o lote vira "Coisa.fbx_01.fbx".
	if (Nome.EndsWith(TEXT(".fbx"), ESearchCase::IgnoreCase)
		|| Nome.EndsWith(TEXT(".usd"), ESearchCase::IgnoreCase)
		|| Nome.EndsWith(TEXT(".usda"), ESearchCase::IgnoreCase)
		|| Nome.EndsWith(TEXT(".usdc"), ESearchCase::IgnoreCase)
		|| Nome.EndsWith(TEXT(".glb"), ESearchCase::IgnoreCase)
		|| Nome.EndsWith(TEXT(".gltf"), ESearchCase::IgnoreCase))
	{
		Nome = FPaths::GetBaseFilename(Nome);
	}

	// Um arquivo so continua sem numero -- numerar sempre baguncava quem ja
	// tem um pipeline apontando para o nome pelado.
	if (Total > 1)
	{
		Nome += FString::Printf(TEXT("_%02d"), Indice + 1);
	}

	return FPaths::Combine(Pasta.Path, Nome + Extensao());
}

#if WITH_EDITOR
void UFofuxoExportOptions::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName Alterada = PropertyChangedEvent.GetPropertyName();

	static const TSet<FName> Espelhados = {
		GET_MEMBER_NAME_CHECKED(UFofuxoExportOptions, EixoParaCima),
		GET_MEMBER_NAME_CHECKED(UFofuxoExportOptions, bFrenteNoEixoX),
		GET_MEMBER_NAME_CHECKED(UFofuxoExportOptions, Unidade),
		GET_MEMBER_NAME_CHECKED(UFofuxoExportOptions, Escala),
	};

	if (Alterada == GET_MEMBER_NAME_CHECKED(UFofuxoExportOptions, Destino))
	{
		AplicarDestino();
	}
	else if (Espelhados.Contains(Alterada))
	{
		GravarNoDestino();
	}
	else if (PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(UFofuxoExportOptions, MeusDestinos))
	{
		// Renomear ou apagar um destino pode ter deixado o escolhido orfao; o
		// AplicarDestino cai no Blender quando nao acha o nome.
		AplicarDestino();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
