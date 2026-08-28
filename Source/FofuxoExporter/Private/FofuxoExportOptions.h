// Fofuxo's Exporter

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/Object.h"

#include "FofuxoExportOptions.generated.h"

class USkeletalMesh;

UENUM()
enum class EFofuxoEixo : uint8
{
	Z UMETA(DisplayName = "Z (Blender, Unreal)"),
	Y UMETA(DisplayName = "Y (Unity, Maya)"),
	X UMETA(DisplayName = "X"),
};

/**
 * O que sai no disco.
 *
 * FBX e o unico que Unreal e Unity leem sem instalar nada, e guarda cada
 * animacao como um take.
 *
 * USD e aberto (Pixar, hoje AOUSD) e guarda cada animacao como um prim
 * SkelAnimation pendurado num esqueleto so. O arquivo sai varias vezes menor,
 * mas quem le costuma tocar so a animacao ligada ao esqueleto: como biblioteca
 * de clipes ele depende do importador do outro lado.
 *
 * glTF 2.0 e aberto (Khronos) e tem um array "animations" nativo, com nome,
 * sobre um unico "skin" -- que e exatamente "um esqueleto, varias animacoes".
 * O importador vem dentro do Blender e cria uma acao para cada. E sempre metros
 * e Y para cima, entao nao ha eixo nem unidade a escolher.
 */
UENUM()
enum class EFofuxoFormato : uint8
{
	FBX UMETA(DisplayName = "FBX (Autodesk)"),
	USD UMETA(DisplayName = "USD (aberto)"),
	GLTF UMETA(DisplayName = "glTF 2.0 (aberto)"),
};

UENUM()
enum class EFofuxoUnidade : uint8
{
	Centimetros UMETA(DisplayName = "Centimetros"),
	Metros UMETA(DisplayName = "Metros"),
};

/** Um jeito de exportar, com nome. Blender e Unity vem prontos; o resto e seu. */
USTRUCT()
struct FFofuxoDestino
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Destino")
	FString Nome = TEXT("Meu destino");

	UPROPERTY(EditAnywhere, Category = "Destino")
	EFofuxoEixo EixoParaCima = EFofuxoEixo::Z;

	UPROPERTY(EditAnywhere, Category = "Destino", meta = (DisplayName = "Frente no eixo X"))
	bool bFrenteNoEixoX = false;

	UPROPERTY(EditAnywhere, Category = "Destino")
	EFofuxoUnidade Unidade = EFofuxoUnidade::Centimetros;

	/** Multiplica em cima da unidade. 2 sai com o dobro do tamanho. */
	UPROPERTY(EditAnywhere, Category = "Destino", meta = (ClampMin = "0.0001", UIMin = "0.01", UIMax = "100.0"))
	float Escala = 1.f;
};

/**
 * As opcoes da janela do Fofuxo's Export.
 *
 * O que tem "config" e lembrado entre sessoes -- inclusive os destinos que voce
 * criar. O resto vem da selecao a cada vez que a janela abre.
 */
UCLASS(config = EditorPerProjectUserSettings)
class UFofuxoExportOptions : public UObject
{
	GENERATED_BODY()

public:
	static const TCHAR* DestinoBlender;
	static const TCHAR* DestinoUnity;

	/** Blender, Unity, ou um dos seus, la de "Meus destinos". */
	UPROPERTY(EditAnywhere, config, Category = "Destino", meta = (GetOptions = "ObterNomesDeDestino"))
	FString Destino;

	UPROPERTY(EditAnywhere, config, Category = "Arquivo", meta = (DisplayName = "Formato"))
	EFofuxoFormato Formato = EFofuxoFormato::FBX;

	UPROPERTY(EditAnywhere, config, Category = "Arquivo", meta = (DisplayName = "Pasta"))
	FDirectoryPath Pasta;

	/**
	 * Vale nos dois formatos. Fica apagado com "Animacoes por arquivo" em 1:
	 * ai cada arquivo leva o nome da animacao que esta dentro dele.
	 */
	UPROPERTY(EditAnywhere, Category = "Arquivo", meta = (DisplayName = "Nome do arquivo", EditCondition = "TakesPorArquivo != 1"))
	FString NomeDoArquivo;

	/**
	 * Quantas animacoes por arquivo. 0 poe todas no mesmo.
	 *
	 * Existe porque arquivo unico nao escala: as animacoes ficam todas vivas na
	 * mesma cena ate o fim, e a escrita e uma chamada so, que nao da para partir
	 * nem cancelar. Medido aqui em FBX com 477 takes: 8,1 GB de pico e 690 MB de
	 * arquivo. Em lotes, o pico cai na proporcao do lote.
	 *
	 * Vale nos dois formatos. Em USD, cada arquivo e um stage com N animacoes
	 * penduradas no mesmo esqueleto -- 1 devolve uma animacao por arquivo.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Arquivo", meta = (DisplayName = "Animacoes por arquivo", ClampMin = "0", UIMin = "0", UIMax = "500"))
	int32 TakesPorArquivo = 100;

	/** O esqueleto e a malha que vao no arquivo. Todas as animacoes tem que ser deste esqueleto. */
	UPROPERTY(EditAnywhere, Category = "Conteudo", meta = (DisplayName = "Skeletal Mesh"))
	TSoftObjectPtr<USkeletalMesh> SkeletalMesh;

	// Os quatro campos abaixo sao o espelho do destino escolhido. Nos destinos
	// prontos eles ficam travados -- o do Blender existe justamente para sair
	// igual ao que a Unreal ja escreve, e mexer nele quebraria isso. Escolha um
	// destino seu e eles destravam.

	UPROPERTY(EditAnywhere, Category = "Avancado", meta = (DisplayName = "Eixo para cima", EditCondition = "bDestinoEhMeu && Formato != EFofuxoFormato::GLTF"))
	EFofuxoEixo EixoParaCima = EFofuxoEixo::Z;

	UPROPERTY(EditAnywhere, Category = "Avancado", meta = (DisplayName = "Frente no eixo X", EditCondition = "bDestinoEhMeu && Formato == EFofuxoFormato::FBX"))
	bool bFrenteNoEixoX = false;

	UPROPERTY(EditAnywhere, Category = "Avancado", meta = (DisplayName = "Unidade", EditCondition = "bDestinoEhMeu && Formato != EFofuxoFormato::GLTF"))
	EFofuxoUnidade Unidade = EFofuxoUnidade::Centimetros;

	UPROPERTY(EditAnywhere, Category = "Avancado", meta = (DisplayName = "Escala", EditCondition = "bDestinoEhMeu", ClampMin = "0.0001", UIMin = "0.01", UIMax = "100.0"))
	float Escala = 1.f;

	/**
	 * Se a malha vai junto com as animacoes. Vale nos tres formatos.
	 *
	 * Desligada, sai so o esqueleto e as curvas: o arquivo fica bem menor e a
	 * exportacao mais rapida, porque converter geometria pesada e o que mais
	 * custa. Serve para quando o outro lado ja tem a malha boa -- e vale
	 * lembrar que malha que passou pela Unreal volta triangulada de qualquer
	 * jeito, entao a original quase sempre e melhor que a exportada.
	 *
	 * E e o formato que a Unity espera para clipe: um FBX com a malha e o rig,
	 * que gera o Avatar, e um arquivo por animacao com Animation Type = Generic
	 * ou Humanoid e Avatar Definition = Copy From Other Avatar. A hierarquia de
	 * ossos sai a mesma nos dois, que e o que faz a copia casar.
	 *
	 * Sem nenhuma animacao marcada, desligar isto nao deixa nada no arquivo --
	 * e ai o botao Exportar fica cinza.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Conteudo", meta = (DisplayName = "Exportar a malha"))
	bool bExportarMalha = true;

	/** Crie aqui, e o nome aparece na lista de Destino la em cima. */
	UPROPERTY(EditAnywhere, config, AdvancedDisplay, Category = "Avancado", meta = (DisplayName = "Meus destinos", TitleProperty = "Nome"))
	TArray<FFofuxoDestino> MeusDestinos;

	/**
	 * Blend shapes do mesh. Desligado por padrao por dois motivos: a engine
	 * escreve as curvas de morph uma vez so, junto com a malha, entao elas so
	 * valeriam para o primeiro take; e o Better FBX cria uma action de shape key
	 * com o mesmo nome da action de pose, o que faz o Blender duplicar tudo com
	 * sufixo .001.
	 *
	 * Sem a malha nao ha blend shape onde pendurar as curvas, e por isso o campo
	 * segue o "Exportar a malha".
	 */
	UPROPERTY(EditAnywhere, config, AdvancedDisplay, Category = "Avancado", meta = (DisplayName = "Exportar morph targets", EditCondition = "Formato == EFofuxoFormato::FBX && bExportarMalha"))
	bool bExportarMorphTargets = false;

	/**
	 * Texto em vez de binario, nos dois formatos: FBX ASCII de um lado, .usda do
	 * outro. Serve para abrir o arquivo num editor de texto e ver o que saiu --
	 * e a unica forma de conferir um USD, que por padrao sai no crate binario e
	 * comprimido. O arquivo fica varias vezes maior.
	 */
	UPROPERTY(EditAnywhere, config, AdvancedDisplay, Category = "Avancado", meta = (DisplayName = "Exportar em texto (ASCII)"))
	bool bASCII = false;

	/**
	 * Caminho das animacoes que voce desmarcou, para a janela lembrar na
	 * proxima vez. Guarda as desmarcadas e nao as marcadas para que uma
	 * animacao nova apareca marcada, que e o padrao util.
	 */
	UPROPERTY(config)
	TArray<FString> Desmarcadas;

	/** Se a lista de animacoes abre expandida ou retraida. */
	UPROPERTY(config)
	bool bListaExpandida = true;

	/** A altura que voce deixou na lista, arrastando a barrinha embaixo dela. */
	UPROPERTY(config)
	float AlturaDaLista = 300.f;

	/** So serve para o EditCondition dos campos espelhados. */
	UPROPERTY(Transient)
	bool bDestinoEhMeu = false;

	UFUNCTION()
	TArray<FString> ObterNomesDeDestino() const;

	/** Copia o destino escolhido para os campos espelhados. */
	void AplicarDestino();

	/** Devolve os campos espelhados para o destino, quando ele e seu. */
	void GravarNoDestino();

	/** A extensao do formato escolhido, com o ponto. */
	FString Extensao() const;

	/**
	 * O caminho completo, com a extensao do formato. Com Total maior que um,
	 * numera: Coisa_01.fbx, Coisa_02.fbx.
	 */
	FString MontarCaminho(int32 Indice = 0, int32 Total = 1) const;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
