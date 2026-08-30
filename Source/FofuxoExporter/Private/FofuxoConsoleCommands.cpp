// Fofuxo
//
// O mesmo export da janela, por linha de comando. Serve para exportar uma pasta
// inteira sem clicar em nada, e e por aqui que da para rodar o plugin com o
// editor em modo -unattended.

#include "FofuxoExportOptions.h"
#include "FofuxoFbxWriter.h"
#include "FofuxoCenaWriter.h"

#include "Animation/AnimSequence.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoExporter, Log, All);

static void FofuxoExportarPorComando(const TArray<FString>& Argumentos)
{
	// Sem a pasta das animacoes sai so a malha -- o mesmo que a janela faz com
	// nenhuma animacao marcada.
	if (Argumentos.Num() < 2)
	{
		UE_LOG(LogFofuxoExporter, Error,
			TEXT("Uso: Fofuxo.Exportar <saida.fbx> <caminho da malha> [pasta das animacoes] [Unity]"));
		return;
	}

	const FString Saida = Argumentos[0];
	const FString CaminhoDaMalha = Argumentos[1];
	const FString PastaDasAnimacoes = Argumentos.Num() > 2 ? Argumentos[2] : FString();

	USkeletalMesh* Malha = LoadObject<USkeletalMesh>(nullptr, *CaminhoDaMalha);
	if (Malha == nullptr)
	{
		UE_LOG(LogFofuxoExporter, Error, TEXT("Nao carreguei o Skeletal Mesh %s"), *CaminhoDaMalha);
		return;
	}

	TArray<UAnimSequence*> Animacoes;

	if (!PastaDasAnimacoes.IsEmpty())
	{
		IAssetRegistry& Registro =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		// So varre se o registro ainda nao terminou de se montar. Varrer sempre, de
		// forma sincrona, obriga a reler o cabecalho de cada .uasset do projeto --
		// num editor recem-aberto isso sozinho custa minutos, e num editor que ja
		// esta de pe nao muda nada porque o registro ja esta pronto.
		if (Registro.IsLoadingAssets())
		{
			Registro.SearchAllAssets(/*bSynchronousSearch*/ true);
		}

		FARFilter Filtro;
		Filtro.ClassPaths.Add(UAnimSequence::StaticClass()->GetClassPathName());
		Filtro.PackagePaths.Add(FName(*PastaDasAnimacoes));
		Filtro.bRecursivePaths = true;

		TArray<FAssetData> Encontrados;
		Registro.GetAssets(Filtro, Encontrados);

		for (const FAssetData& Asset : Encontrados)
		{
			if (UAnimSequence* Sequencia = Cast<UAnimSequence>(Asset.GetAsset()))
			{
				if (Sequencia->GetSkeleton() == Malha->GetSkeleton())
				{
					Animacoes.Add(Sequencia);
				}
			}
		}

		if (Animacoes.Num() == 0)
		{
			UE_LOG(LogFofuxoExporter, Error,
				TEXT("Nenhuma Animation Sequence do esqueleto de %s em %s"), *Malha->GetName(), *PastaDasAnimacoes);
			return;
		}

		Animacoes.Sort([](const UAnimSequence& A, const UAnimSequence& B)
		{
			return A.GetName() < B.GetName();
		});
	}
	else
	{
		UE_LOG(LogFofuxoExporter, Display,
			TEXT("Sem pasta de animacoes: exportando so a malha %s"), *Malha->GetName());
	}

	UFofuxoExportOptions* Opcoes = NewObject<UFofuxoExportOptions>();
	TStrongObjectPtr<UFofuxoExportOptions> Guarda(Opcoes);

	// Aceita tambem o nome de um destino seu, gravado pela janela.
	Opcoes->LoadConfig();
	Opcoes->Destino = Argumentos.Num() > 3 ? Argumentos[3] : UFofuxoExportOptions::DestinoBlender;
	Opcoes->AplicarDestino();

	UE_LOG(LogFofuxoExporter, Display, TEXT("Destinos seus carregados: %d"), Opcoes->MeusDestinos.Num());
	for (const FFofuxoDestino& Meu : Opcoes->MeusDestinos)
	{
		UE_LOG(LogFofuxoExporter, Display, TEXT("  \"%s\" eixo=%d unidade=%d escala=%f"),
			*Meu.Nome, (int32)Meu.EixoParaCima, (int32)Meu.Unidade, Meu.Escala);
	}
	UE_LOG(LogFofuxoExporter, Display, TEXT("Aplicado \"%s\" (seu=%d) eixo=%d unidade=%d escala=%f"),
		*Opcoes->Destino, Opcoes->bDestinoEhMeu ? 1 : 0,
		(int32)Opcoes->EixoParaCima, (int32)Opcoes->Unidade, Opcoes->Escala);

	FFofuxoExportPedido Pedido;
	Pedido.Animacoes = Animacoes;
	Pedido.SkeletalMesh = Malha;
	Pedido.CaminhoDoArquivo = Saida;
	Pedido.Opcoes = Opcoes;

	// O formato vem do ini, igual ao da janela -- o comando existe para repetir
	// o que ela faz, nao para ter regra propria.
	FText Erro;

	const bool bDeuCerto = Opcoes->Formato == EFofuxoFormato::FBX
		? FFofuxoFbxWriter::Exportar(Pedido, Erro)
		: FFofuxoCenaWriter::Exportar(Pedido, Erro);

	if (!bDeuCerto)
	{
		UE_LOG(LogFofuxoExporter, Error, TEXT("Falhou: %s"), *Erro.ToString());
		return;
	}

	if (Animacoes.Num() == 0)
	{
		UE_LOG(LogFofuxoExporter, Display, TEXT("Fofuxo: so a malha %s escrita em %s"), *Malha->GetName(), *Saida);
	}
	else
	{
		UE_LOG(LogFofuxoExporter, Display,
			TEXT("Fofuxo: %d animacoes escritas a partir de %s"), Animacoes.Num(), *Saida);
	}
}

static FAutoConsoleCommand GFofuxoExportar(
	TEXT("Fofuxo.Exportar"),
	TEXT("Fofuxo.Exportar <saida.fbx> <caminho da malha> [pasta das animacoes] [Unity]"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&FofuxoExportarPorComando));
