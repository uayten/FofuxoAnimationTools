// Fofuxo's Exporter -- cena USD
//
// Escreve varias animacoes num stage USD so: um SkelRoot, um Skeleton, e um
// prim SkelAnimation por animacao.
//
// Por que isto e um modulo separado: as conversoes da engine recebem tipos da
// Pixar (pxr::UsdPrim), o que obriga a linkar contra o SDK. Deixar isso no
// modulo principal faria o plugin inteiro exigir o USD ligado. Aqui, se o USD
// nao estiver presente, este modulo simplesmente nao entrega o delegate e o
// resto do plugin segue exportando FBX.

#include "FofuxoCenaUsd.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Modules/ModuleManager.h"
#include "ReferenceSkeleton.h"

#if USE_USD_SDK
#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDLayerUtils.h"
#include "USDObjectUtils.h"
#include "USDSkeletalDataConversion.h"
#include "USDStageOptions.h"
#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#include "USDMemory.h"

#include "USDIncludesStart.h"
#include "pxr/usd/usdSkel/animation.h"
#include "pxr/usd/usdSkel/skeleton.h"
#include "USDIncludesEnd.h"
#endif	  // USE_USD_SDK

#define LOCTEXT_NAMESPACE "FofuxoUsdCena"

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoUsdCena, Log, All);

#if USE_USD_SDK
namespace FofuxoCena
{
	/**
	 * Nome de prim valido em USD: letras, numeros e sublinhado, sem comecar por
	 * numero. Os nomes de asset da Unreal quase sempre ja passam; quase nao e
	 * sempre, e um nome invalido faz o DefinePrim voltar vazio.
	 */
	static FString NomeDePrim(const FString& Nome)
	{
		FString Limpo;
		Limpo.Reserve(Nome.Len() + 1);

		for (const TCHAR Letra : Nome)
		{
			Limpo.AppendChar(FChar::IsAlnum(Letra) || Letra == TEXT('_') ? Letra : TEXT('_'));
		}

		if (Limpo.IsEmpty() || FChar::IsDigit(Limpo[0]))
		{
			Limpo.InsertAt(0, TEXT('_'));
		}

		return Limpo;
	}

	/**
	 * Um nome que o USD aceita como identificador: comeca por letra ou
	 * sublinhado, e depois so letra, numero ou sublinhado.
	 *
	 * A limpeza da engine (SanitizeObjectName) nao basta. Ela troca o conjunto
	 * de caracteres invalidos para nome de objeto da Unreal -- que inclui ponto
	 * e barra, mas deixa passar hifen, acento e nome comecando por digito. Um
	 * osso "arm-l" ou "cabeca" com cedilha atravessa aquela funcao e vira um
	 * caminho de junta que o USD considera invalido, e ai quem le recusa o
	 * arquivo inteiro.
	 */
	static FString IdentificadorUsd(const FString& Nome)
	{
		FString Saida;
		Saida.Reserve(Nome.Len() + 1);

		for (const TCHAR Letra : Nome)
		{
			const bool bAceito = (Letra >= TEXT('a') && Letra <= TEXT('z'))
				|| (Letra >= TEXT('A') && Letra <= TEXT('Z'))
				|| (Letra >= TEXT('0') && Letra <= TEXT('9'))
				|| Letra == TEXT('_');

			Saida.AppendChar(bAceito ? Letra : TEXT('_'));
		}

		if (Saida.IsEmpty() || (Saida[0] >= TEXT('0') && Saida[0] <= TEXT('9')))
		{
			Saida.InsertAt(0, TEXT('_'));
		}

		return Saida;
	}

	/**
	 * Os caminhos de junta, na ordem dos ossos, ja validos e unicos.
	 *
	 * Vai para o prim de Skeleton e para o de cada animacao, iguaizinhos: os dois
	 * arrays sao indexados pela mesma ordem de osso, entao trocar os dois pela
	 * mesma lista mantem tudo alinhado com as matrizes de transformacao.
	 */
	static TArray<FString> CaminhosDeJunta(const FReferenceSkeleton& Referencia, TArray<FString>& OutRenomeados)
	{
		const TArray<FMeshBoneInfo>& Ossos = Referencia.GetRefBoneInfo();

		TArray<FString> Caminhos;
		Caminhos.SetNum(Ossos.Num());

		TSet<FString> JaUsados;

		for (int32 Indice = 0; Indice < Ossos.Num(); ++Indice)
		{
			const FString Original = Ossos[Indice].Name.ToString();
			FString Nome = IdentificadorUsd(Original);

			// Dois irmaos que caiam no mesmo nome depois da limpeza precisam se
			// separar, senao viram o mesmo caminho.
			const int32 Pai = Ossos[Indice].ParentIndex;
			const FString Prefixo = Caminhos.IsValidIndex(Pai) && Pai >= 0 ? Caminhos[Pai] + TEXT("/") : FString();

			FString Tentativa = Prefixo + Nome;
			int32 Sufixo = 2;
			while (JaUsados.Contains(Tentativa))
			{
				Tentativa = Prefixo + Nome + FString::Printf(TEXT("_%d"), Sufixo++);
			}

			JaUsados.Add(Tentativa);
			Caminhos[Indice] = Tentativa;

			const FString Folha = Tentativa.RightChop(Prefixo.Len());
			if (Folha != Original)
			{
				OutRenomeados.Add(FString::Printf(TEXT("%s -> %s"), *Original, *Folha));
			}
		}

		return Caminhos;
	}

	/** Poe a lista de juntas no atributo, por cima do que a engine escreveu. */
	static void EscreverJuntas(const TArray<FString>& Caminhos, pxr::UsdAttribute Atributo)
	{
		if (!Atributo)
		{
			return;
		}

		FScopedUsdAllocs Allocs;

		pxr::VtArray<pxr::TfToken> Juntas;
		Juntas.reserve(Caminhos.Num());

		for (const FString& Caminho : Caminhos)
		{
			Juntas.push_back(pxr::TfToken(TCHAR_TO_UTF8(*Caminho)));
		}

		Atributo.Set(Juntas);
	}

	/**
	 * A malha de onde a conversao da engine vai tirar as juntas da animacao.
	 *
	 * Nao e a malha escolhida na janela: o ConvertAnimSequence procura sozinho a
	 * preview mesh do esqueleto da animacao e, na falta dela, qualquer malha
	 * compativel. Escrever o prim de Skeleton a partir de outra malha deixaria o
	 * arquivo com dois conjuntos de juntas que nao conversam.
	 */
	static USkeletalMesh* MalhaQueAEngineVaiUsar(UAnimSequence* Sequencia)
	{
		USkeleton* EsqueletoDela = Sequencia != nullptr ? Sequencia->GetSkeleton() : nullptr;
		if (EsqueletoDela == nullptr)
		{
			return nullptr;
		}

		if (USkeletalMesh* DePreview = EsqueletoDela->GetAssetPreviewMesh(Sequencia))
		{
			return DePreview;
		}

		return EsqueletoDela->FindCompatibleMesh();
	}

	static bool Escrever(const FFofuxoPedidoDeCena& Pedido, FText& OutErro)
	{
		// Lista vazia sai um stage so com o SkelRoot, o esqueleto e a malha --
		// nenhum prim de SkelAnimation, e nenhum intervalo de tempo.
		if (Pedido.Malha == nullptr)
		{
			OutErro = LOCTEXT("CenaSemMalha", "Sem Skeletal Mesh nao da para escrever o esqueleto da cena.");
			return false;
		}

		UE::FUsdStage Palco = UnrealUSDWrapper::NewStage(*Pedido.Caminho);
		if (!Palco)
		{
			OutErro = FText::Format(
				LOCTEXT("CenaSemPalco", "Nao consegui criar o stage USD em {0}."),
				FText::FromString(Pedido.Caminho));
			return false;
		}

		UsdUtils::SetUsdStageMetersPerUnit(Palco, Pedido.MetrosPorUnidade);
		UsdUtils::SetUsdStageUpAxis(Palco, Pedido.bYParaCima ? EUsdUpAxis::YAxis : EUsdUpAxis::ZAxis);

		// A taxa do stage vale para todo mundo: o ConvertAnimSequence assa as
		// chaves na resolucao dela. Por isso a maior taxa do lote, e nao a da
		// primeira animacao -- com a da primeira, uma animacao de 60 num arquivo
		// que abriu com uma de 30 perderia metade das chaves.
		double QuadrosPorSegundo = 0.0;
		double DuracaoMaisLonga = 0.0;

		for (const UAnimSequence* Sequencia : Pedido.Animacoes)
		{
			if (Sequencia == nullptr)
			{
				continue;
			}

			QuadrosPorSegundo = FMath::Max(QuadrosPorSegundo, Sequencia->GetSamplingFrameRate().AsDecimal());
			DuracaoMaisLonga = FMath::Max(DuracaoMaisLonga, static_cast<double>(Sequencia->GetPlayLength()));
		}

		if (QuadrosPorSegundo <= 0.0)
		{
			QuadrosPorSegundo = 30.0;
		}

		Palco.SetTimeCodesPerSecond(QuadrosPorSegundo);

		const UE::FSdfPath CaminhoDaRaiz(TEXT("/Root"));

		UE::FUsdPrim Raiz = Palco.DefinePrim(CaminhoDaRaiz, TEXT("SkelRoot"));
		if (!Raiz)
		{
			OutErro = LOCTEXT("CenaSemRaiz", "Nao consegui criar o SkelRoot da cena.");
			return false;
		}

		Palco.SetDefaultPrim(Raiz);

		// O esqueleto do arquivo tem que ser o mesmo que as animacoes vao citar.
		// Sem animacao nenhuma quem manda e a malha escolhida na janela.
		USkeletalMesh* MalhaDoEsqueleto = Pedido.Animacoes.Num() > 0
			? MalhaQueAEngineVaiUsar(Pedido.Animacoes[0])
			: nullptr;

		if (MalhaDoEsqueleto == nullptr)
		{
			MalhaDoEsqueleto = Pedido.Malha;
		}

		// Duas animacoes que resolvam para malhas diferentes nao cabem no mesmo
		// arquivo: so ha um prim de Skeleton, e uma das duas ficaria citando
		// juntas que nao existem nele.
		for (UAnimSequence* Sequencia : Pedido.Animacoes)
		{
			USkeletalMesh* Dela = MalhaQueAEngineVaiUsar(Sequencia);
			if (Dela != nullptr && Dela != MalhaDoEsqueleto)
			{
				OutErro = FText::Format(
					LOCTEXT("CenaDuasMalhas",
						"{0} usa a malha {1}, mas o arquivo esta sendo escrito com o esqueleto de {2}. "
						"Exporte esses grupos separados, ou acerte a Preview Mesh do esqueleto."),
					FText::FromString(Sequencia->GetName()),
					FText::FromString(Dela->GetName()),
					FText::FromString(MalhaDoEsqueleto->GetName()));
				return false;
			}
		}

		const FReferenceSkeleton& Referencia = MalhaDoEsqueleto->GetRefSkeleton();
		if (Referencia.GetRefBoneInfo().Num() == 0)
		{
			OutErro = FText::Format(
				LOCTEXT("CenaSemOssos", "O esqueleto de {0} nao tem osso nenhum."),
				FText::FromString(MalhaDoEsqueleto->GetName()));
			return false;
		}

		TArray<FString> Renomeados;
		const TArray<FString> Juntas = CaminhosDeJunta(Referencia, Renomeados);

		if (Renomeados.Num() > 0)
		{
			UE_LOG(LogFofuxoUsdCena, Display,
				TEXT("%d osso(s) tiveram o nome ajustado para caber no USD. O arquivo usa os nomes ajustados:"),
				Renomeados.Num());

			for (const FString& Troca : Renomeados)
			{
				UE_LOG(LogFofuxoUsdCena, Display, TEXT("    %s"), *Troca);
			}
		}

		// A malha entra pela conversao da engine, que ja cria dentro do SkelRoot o
		// esqueleto, os prims de Mesh e as blend shapes, tudo amarrado. Escrever o
		// esqueleto por fora daria dois prims -- o dela se chama "Skel", nao
		// "Skeleton", e a malha ficaria presa no dela e as animacoes no meu.
		//
		// So o LOD 0: os outros multiplicam o arquivo sem servir para animacao.
		{
			pxr::UsdPrim RaizCrua{Raiz};
			if (!UnrealToUsd::ConvertSkeletalMesh(MalhaDoEsqueleto, RaizCrua, pxr::UsdTimeCode::Default(), nullptr, 0, 0))
			{
				OutErro = FText::Format(
					LOCTEXT("CenaMalhaFalhou", "A engine nao converteu a malha {0}."),
					FText::FromString(MalhaDoEsqueleto->GetName()));
				return false;
			}
		}

		UE::FUsdPrim Esqueleto = Palco.GetPrimAtPath(
			CaminhoDaRaiz.AppendChild(UnrealIdentifiers::ExportedSkeletonPrimName));

		if (!Esqueleto)
		{
			OutErro = LOCTEXT("CenaSemEsqueleto", "A conversao da malha nao deixou um prim de esqueleto na cena.");
			return false;
		}

		{
			// Por cima do que a engine escreveu: a limpeza dela deixa passar nome
			// que o USD nao aceita.
			pxr::UsdSkelSkeleton UsdEsqueleto{pxr::UsdPrim(Esqueleto)};
			EscreverJuntas(Juntas, UsdEsqueleto.CreateJointsAttr());
		}

		for (int32 Indice = 0; Indice < Pedido.Animacoes.Num(); ++Indice)
		{
			UAnimSequence* Sequencia = Pedido.Animacoes[Indice];
			if (Sequencia == nullptr)
			{
				continue;
			}

			const FString Nome = NomeDePrim(Sequencia->GetName());

			UE::FUsdPrim PrimDaAnimacao = Palco.DefinePrim(CaminhoDaRaiz.AppendChild(*Nome), TEXT("SkelAnimation"));
			if (!PrimDaAnimacao)
			{
				OutErro = FText::Format(
					LOCTEXT("CenaSemPrim", "Nao consegui criar o prim da animacao {0}."),
					FText::FromString(Sequencia->GetName()));
				return false;
			}

			if (!UnrealToUsd::ConvertAnimSequence(Sequencia, PrimDaAnimacao))
			{
				OutErro = FText::Format(
					LOCTEXT("CenaAnimacaoFalhou", "A engine nao converteu a animacao {0}."),
					FText::FromString(Sequencia->GetName()));
				return false;
			}

			// A mesma lista do esqueleto, pelo mesmo motivo -- e e o que mantem os
			// dois prims falando dos mesmos ossos.
			{
				pxr::UsdSkelAnimation UsdAnimacao{pxr::UsdPrim(PrimDaAnimacao)};
				EscreverJuntas(Juntas, UsdAnimacao.CreateJointsAttr());
			}

			// Um stage tem uma linha do tempo so, e um esqueleto toca uma
			// animacao de cada vez. As outras ficam no arquivo esperando que
			// quem le troque o skel:animationSource -- que e como USD faz
			// biblioteca de clipes. A primeira vem ligada para o arquivo abrir
			// mostrando alguma coisa.
			if (Indice == 0)
			{
				UsdUtils::BindAnimationSource(Esqueleto, PrimDaAnimacao);
			}
		}

		// O fim do intervalo em timecodes do stage: duracao em segundos vezes a
		// taxa. Contar quadros da animacao daria errado assim que duas taxas
		// diferentes convivessem no mesmo arquivo.
		//
		// So a malha nao tem linha do tempo: escrever um intervalo 0-0 faria quem
		// le mostrar um quadro de animacao que nao existe.
		if (Pedido.Animacoes.Num() > 0)
		{
			UsdUtils::AddTimeCodeRangeToLayer(
				Palco.GetRootLayer(),
				0.0,
				FMath::CeilToDouble(DuracaoMaisLonga * QuadrosPorSegundo));
		}

		if (!Palco.GetRootLayer().Save())
		{
			OutErro = FText::Format(
				LOCTEXT("CenaNaoSalvou", "O arquivo {0} nao foi escrito."),
				FText::FromString(Pedido.Caminho));
			return false;
		}

		return true;
	}
}
#endif	  // USE_USD_SDK

class FFofuxoUsdCenaModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if USE_USD_SDK
		FofuxoEscritorDeCenaUsd().BindStatic(&FofuxoCena::Escrever);
#endif
	}

	virtual void ShutdownModule() override
	{
		// Obrigatorio: o delegate vive no outro modulo e apontaria para codigo
		// desta DLL depois que ela sair.
		FofuxoEscritorDeCenaUsd().Unbind();
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFofuxoUsdCenaModule, FofuxoUsdCena)
