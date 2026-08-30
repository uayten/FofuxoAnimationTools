// Fofuxo

#include "FofuxoFbxWriter.h"
#include "FofuxoNome.h"

#include "FofuxoExportOptions.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Exporters/FbxExportOption.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopedSlowTask.h"
#include "ReferenceSkeleton.h"
#include "UObject/StrongObjectPtr.h"

// Privado do UnrealEd. Ver o comentario no Build.cs.
#include "FbxExporter.h"

#define LOCTEXT_NAMESPACE "FofuxoExporter"

namespace FofuxoPrivado
{
	/**
	 * O metodo que escreve as curvas de um take ja existe na engine, aceita um
	 * FbxAnimLayer explicito e faz exatamente o que a gente precisa -- so que e
	 * privado. Este e o truque padrao de instanciacao explicita: o compilador
	 * nao checa acesso ao instanciar um template, entao da para guardar o
	 * ponteiro para o membro e chamar por ele.
	 *
	 * A alternativa seria reimplementar a amostragem de curvas de osso, que e
	 * onde moram os detalhes chatos (conversao de eixo, root motion, atributos
	 * customizados). Reaproveitar garante que o take 2 saia igualzinho ao take 1.
	 */
	template <typename Etiqueta, typename Etiqueta::Tipo Membro>
	struct TLadrao
	{
		friend typename Etiqueta::Tipo Roubar(Etiqueta)
		{
			return Membro;
		}
	};

	struct FExportarTake
	{
		using Tipo = void (UnFbx::FFbxExporter::*)(
			const UAnimSequence*, const USkeletalMesh*, TArray<FbxNode*>&,
			FbxAnimLayer*, FFrameTime, FFrameTime, float, float);

		friend Tipo Roubar(FExportarTake);
	};
	template struct TLadrao<FExportarTake, &UnFbx::FFbxExporter::ExportAnimSequenceToFbx>;

	struct FCorrigirInterpolacao
	{
		using Tipo = void (UnFbx::FFbxExporter::*)(TArray<FbxNode*>&, FbxAnimLayer*);

		friend Tipo Roubar(FCorrigirInterpolacao);
	};
	template struct TLadrao<FCorrigirInterpolacao, &UnFbx::FFbxExporter::CorrectAnimTrackInterpolation>;

	/**
	 * A cena do documento aberto. O mesmo truque, agora num membro de dado.
	 *
	 * Existe por causa do export so da malha: o ExportSkeletalMesh da engine e
	 * publico mas nao devolve no nenhum, e sem a cena nao da para converter eixo
	 * e unidade. No caminho com animacao a cena continua vindo do no de osso.
	 */
	struct FCena
	{
		using Tipo = FbxScene* UnFbx::FFbxExporter::*;

		friend Tipo Roubar(FCena);
	};
	template struct TLadrao<FCena, &UnFbx::FFbxExporter::Scene>;

	// Funcao amiga definida dentro da classe so aparece por ADL. Redeclarar no
	// escopo do namespace deixa a chamada qualificada funcionar tambem.
	FExportarTake::Tipo Roubar(FExportarTake);
	FCorrigirInterpolacao::Tipo Roubar(FCorrigirInterpolacao);
	FCena::Tipo Roubar(FCena);

	/**
	 * Remonta o array de nos de osso na mesma ordem que o CreateSkeleton da
	 * engine usou -- indice do FReferenceSkeleton -- descendo a hierarquia a
	 * partir da raiz que o ExportAnimSequence devolveu.
	 *
	 * Procurar por nome na cena inteira seria mais curto, mas casaria com
	 * qualquer no homonimo; descer pelo pai certo nao tem esse risco.
	 */
	static bool ColetarOssos(const USkeletalMesh* Malha, FbxNode* RaizDosOssos, TArray<FbxNode*>& Saida, FString& OutOssoQueFaltou)
	{
		const FReferenceSkeleton& Referencia = Malha->GetRefSkeleton();
		const int32 NumOssos = Referencia.GetRawBoneNum();

		Saida.Reset();
		if (NumOssos == 0 || RaizDosOssos == nullptr)
		{
			return false;
		}

		Saida.Add(RaizDosOssos);

		for (int32 Indice = 1; Indice < NumOssos; ++Indice)
		{
			const FMeshBoneInfo& Osso = Referencia.GetRefBoneInfo()[Indice];

#if WITH_EDITORONLY_DATA
			const FbxString NomeEsperado = UnFbx::FFbxDataConverter::ConvertToFbxString(Osso.ExportName);
#else
			const FbxString NomeEsperado = UnFbx::FFbxDataConverter::ConvertToFbxString(Osso.Name);
#endif

			FbxNode* Pai = Saida.IsValidIndex(Osso.ParentIndex) ? Saida[Osso.ParentIndex] : nullptr;
			FbxNode* Encontrado = nullptr;

			if (Pai != nullptr)
			{
				for (int32 Filho = 0; Filho < Pai->GetChildCount(); ++Filho)
				{
					FbxNode* No = Pai->GetChild(Filho);
					if (FbxString(No->GetName()) == NomeEsperado)
					{
						Encontrado = No;
						break;
					}
				}
			}

			if (Encontrado == nullptr)
			{
				OutOssoQueFaltou = Osso.Name.ToString();
				return false;
			}

			Saida.Add(Encontrado);
		}

		return true;
	}

	/** O intervalo de tempo de um take, em segundos. */
	static FbxTimeSpan IntervaloDe(const UAnimSequence* Sequencia)
	{
		const int32 NumQuadros = Sequencia->GetDataModel()->GetNumberOfFrames();

		FbxTime Inicio;
		FbxTime Fim;
		Inicio.SetSecondDouble(0.0);
		Fim.SetSecondDouble(Sequencia->GetDataModel()->GetFrameRate().AsSeconds(NumQuadros));

		return FbxTimeSpan(Inicio, Fim);
	}
}

static bool EscreverUmArquivo(const FFofuxoExportPedido& Pedido, FScopedSlowTask& Progresso, FText& OutErro)
{
	// Lista vazia nao e erro: e o pedido de "so a malha". O que nao pode faltar
	// nunca e o Skeletal Mesh -- e dele que sai a geometria e o esqueleto.
	const bool bSoAMalha = Pedido.Animacoes.Num() == 0;

	if (Pedido.SkeletalMesh == nullptr)
	{
		OutErro = LOCTEXT("SemMalha", "Escolha o Skeletal Mesh que vai junto no arquivo.");
		return false;
	}

	if (Pedido.Opcoes == nullptr)
	{
		OutErro = LOCTEXT("SemOpcoes", "Opcoes de export ausentes.");
		return false;
	}

	const USkeleton* Esqueleto = Pedido.SkeletalMesh->GetSkeleton();
	for (const UAnimSequence* Sequencia : Pedido.Animacoes)
	{
		if (Sequencia == nullptr)
		{
			OutErro = LOCTEXT("AnimacaoNula", "Uma das animacoes nao pode ser carregada.");
			return false;
		}

		if (Sequencia->GetSkeleton() != Esqueleto)
		{
			OutErro = FText::Format(
				LOCTEXT("EsqueletoDiferente", "A animacao {0} e de outro esqueleto, nao do esqueleto de {1}."),
				FText::FromString(Sequencia->GetName()),
				FText::FromString(Pedido.SkeletalMesh->GetName()));
			return false;
		}
	}

	// A pasta pode nao existir ainda.
	const FString Pasta = FPaths::GetPath(Pedido.CaminhoDoArquivo);
	if (!Pasta.IsEmpty() && !IFileManager::Get().DirectoryExists(*Pasta))
	{
		IFileManager::Get().MakeDirectory(*Pasta, /*Tree*/ true);
	}

	UnFbx::FFbxExporter* Exportador = UnFbx::FFbxExporter::GetInstance();
	if (Exportador == nullptr)
	{
		OutErro = LOCTEXT("SemExportador", "Nao consegui chegar no exportador de FBX da engine.");
		return false;
	}

	// Sem isso a engine abre o dialogo de opcoes dela por cima do nosso.
	UFbxExportOption* OpcoesDaEngine = NewObject<UFbxExportOption>();
	TStrongObjectPtr<UFbxExportOption> Guarda(OpcoesDaEngine);

	OpcoesDaEngine->FbxExportCompatibility = EFbxExportCompatibility::FBX_2020;
	OpcoesDaEngine->bASCII = Pedido.Opcoes->bASCII;
	OpcoesDaEngine->bForceFrontXAxis = Pedido.Opcoes->bFrenteNoEixoX;
	// Quando a malha vai no arquivo, e este campo que faz a engine mandar as
	// curvas de morph target pelos blend shapes dela em vez de virarem
	// propriedades soltas no osso raiz. Sem malha nao ha blend shape nenhum, e
	// morph target ligado so sujaria o esqueleto -- dai os dois andarem juntos.
	OpcoesDaEngine->bExportPreviewMesh = Pedido.Opcoes->bExportarMalha;
	OpcoesDaEngine->bExportMorphTargets =
		Pedido.Opcoes->bExportarMalha && Pedido.Opcoes->bExportarMorphTargets;
	OpcoesDaEngine->bExportLocalTime = true;

	Exportador->SetExportOptionsOverride(OpcoesDaEngine);
	ON_SCOPE_EXIT
	{
		Exportador->SetExportOptionsOverride(nullptr);
	};

	const FString NomeDaMalha = Pedido.SkeletalMesh->GetName();

	FbxScene* Cena = nullptr;
	TArray<FbxNode*> Ossos;
	TArray<FbxAnimStack*> Stacks;

	if (bSoAMalha)
	{
		Progresso.EnterProgressFrame(1.f, FText::Format(
			LOCTEXT("SoAMalha", "Escrevendo a malha {0}"),
			FText::FromString(NomeDaMalha)));

		Exportador->CreateDocument();

		// Sem take nenhum, quem escreve e o export de malha da propria engine --
		// o mesmo do "Export..." do Content Browser. Ele monta esqueleto, bind
		// pose e geometria, e nao cria stack de animacao.
		Exportador->ExportSkeletalMesh(Pedido.SkeletalMesh);

		Cena = Exportador->*FofuxoPrivado::Roubar(FofuxoPrivado::FCena());
		if (Cena == nullptr)
		{
			Exportador->CloseDocument();
			OutErro = FText::Format(
				LOCTEXT("FalhouSoAMalha", "A engine nao conseguiu exportar a malha {0}."),
				FText::FromString(NomeDaMalha));
			return false;
		}
	}
	else
	{
		Progresso.EnterProgressFrame(1.f, FText::Format(
			LOCTEXT("PrimeiroTake", "{0}"),
			FText::FromString(Pedido.Animacoes[0]->GetName())));

		Exportador->CreateDocument();

		// O primeiro take sai pelo caminho normal da engine, e traz junto o
		// esqueleto e -- se a malha estiver ligada -- a geometria e o bind pose.
		// Com ela desligada a engine monta o esqueleto e as curvas do mesmo
		// jeito, e devolve a mesma raiz de osso; so pula a geometria.
		FbxNode* RaizDosOssos = Exportador->ExportAnimSequence(
			Pedido.Animacoes[0], Pedido.SkeletalMesh,
			/*bExportSkelMesh*/ Pedido.Opcoes->bExportarMalha, *NomeDaMalha);

		if (RaizDosOssos == nullptr)
		{
			Exportador->CloseDocument();
			OutErro = FText::Format(
				LOCTEXT("FalhouPrimeiroTake", "A engine nao conseguiu exportar {0}."),
				FText::FromString(Pedido.Animacoes[0]->GetName()));
			return false;
		}

		Cena = RaizDosOssos->GetScene();
		if (Cena == nullptr)
		{
			Exportador->CloseDocument();
			OutErro = LOCTEXT("SemCena", "O no de osso voltou sem cena.");
			return false;
		}

		FString OssoQueFaltou;
		if (!FofuxoPrivado::ColetarOssos(Pedido.SkeletalMesh, RaizDosOssos, Ossos, OssoQueFaltou))
		{
			Exportador->CloseDocument();
			OutErro = FText::Format(
				LOCTEXT("OssoSumido", "Nao achei o osso {0} na hierarquia exportada."),
				FText::FromString(OssoQueFaltou));
			return false;
		}

		// O stack que a engine criou no CreateDocument se chama "Unreal Take". E
		// esse nome, igual em todo arquivo, que faz o Blender numerar as actions.
		FbxAnimStack* PrimeiroStack = Cena->GetSrcObject<FbxAnimStack>(0);
		if (PrimeiroStack == nullptr)
		{
			Exportador->CloseDocument();
			OutErro = LOCTEXT("SemStack", "O documento saiu sem nenhum take.");
			return false;
		}

		PrimeiroStack->SetName(TCHAR_TO_UTF8(*Pedido.Animacoes[0]->GetName()));
		Stacks.Add(PrimeiroStack);

		// Do segundo take em diante e por nossa conta: um stack e um layer para cada.
		for (int32 Indice = 1; Indice < Pedido.Animacoes.Num(); ++Indice)
		{
			const UAnimSequence* Sequencia = Pedido.Animacoes[Indice];
			const FString Nome = Sequencia->GetName();

			Progresso.EnterProgressFrame(1.f, FText::FromString(Nome));

			// Desistir aqui nao deixa lixo: o arquivo so nasce no WriteToFile, la
			// embaixo, e o documento morre com o CloseDocument.
			if (Progresso.ShouldCancel())
			{
				Exportador->CloseDocument();
				OutErro = LOCTEXT("Cancelou", "Exportacao cancelada. Nenhum arquivo foi escrito.");
				return false;
			}

			FbxAnimStack* Stack = FbxAnimStack::Create(Cena, TCHAR_TO_UTF8(*Nome));
			FbxAnimLayer* Layer = FbxAnimLayer::Create(Cena, "Base Layer");
			Stack->AddMember(Layer);

			const int32 NumQuadros = Sequencia->GetDataModel()->GetNumberOfFrames();

			(Exportador->*FofuxoPrivado::Roubar(FofuxoPrivado::FExportarTake()))(
				Sequencia,
				Pedido.SkeletalMesh,
				Ossos,
				Layer,
				FFrameTime(0),
				FFrameTime(NumQuadros),
				/*FrameRateScale*/ 1.f,
				/*StartTime*/ 0.f);

			(Exportador->*FofuxoPrivado::Roubar(FofuxoPrivado::FCorrigirInterpolacao()))(Ossos, Layer);

			Stacks.Add(Stack);
		}
	}

	// O SetupAnimStack da engine escreve o intervalo sempre no stack do
	// documento, nao no layer que a gente passou -- entao o take 1 ficou com a
	// duracao da ultima animacao. Corrige todo mundo aqui, no fim.
	for (int32 Indice = 0; Indice < Stacks.Num(); ++Indice)
	{
		// SetLocalTimeSpan pede referencia nao-const, entao precisa de um lvalue.
		FbxTimeSpan Intervalo = FofuxoPrivado::IntervaloDe(Pedido.Animacoes[Indice]);
		Stacks[Indice]->SetLocalTimeSpan(Intervalo);
	}

	// Converter a cena e escrever sao as duas partes que nao dao para partir em
	// pedacos: dai serem uma etapa so, anunciada antes de comecar.
	Progresso.EnterProgressFrame(1.f, bSoAMalha
		? FText::Format(
			LOCTEXT("EscrevendoSoAMalha", "Escrevendo {0}"),
			FText::FromString(FPaths::GetCleanFilename(Pedido.CaminhoDoArquivo)))
		: FText::Format(
			LOCTEXT("Escrevendo", "Escrevendo {0} takes em {1}"),
			FText::AsNumber(Pedido.Animacoes.Num()),
			FText::FromString(FPaths::GetCleanFilename(Pedido.CaminhoDoArquivo))));

	// Eixo, unidade e escala. Os tres so mexem na cena se o destino pedir algo
	// diferente do que a Unreal ja escreveu -- no destino Blender, que e igual
	// ao export nativo, nenhum deles encosta em nada.
	{
		FbxAxisSystem::EUpVector Cima = FbxAxisSystem::eZAxis;
		switch (Pedido.Opcoes->EixoParaCima)
		{
		case EFofuxoEixo::X: Cima = FbxAxisSystem::eXAxis; break;
		case EFofuxoEixo::Y: Cima = FbxAxisSystem::eYAxis; break;
		default:             Cima = FbxAxisSystem::eZAxis; break;
		}

		// So o eixo de cima e nosso; a frente e a mao ficam como a Unreal
		// escreveu. Montar o sistema inteiro na mao trocava o sinal da frente
		// (a Unreal usa eParityOdd negativo), a comparacao dava sempre
		// diferente, e um DeepConvertScene rodava sem necessidade -- era isso
		// que chegava no Blender como uma rotacao de permutacao de eixos.
		const FbxAxisSystem Atual = Cena->GetGlobalSettings().GetAxisSystem();

		int32 SinalDeCima = 1;
		const FbxAxisSystem::EUpVector CimaAtual = Atual.GetUpVector(SinalDeCima);

		if (CimaAtual != Cima)
		{
			int32 SinalDaFrente = 1;
			const FbxAxisSystem::EFrontVector FrenteAtual = Atual.GetFrontVector(SinalDaFrente);

			// O sinal vai embutido no valor do enum: negativo inverte o eixo.
			const FbxAxisSystem Desejado(
				(FbxAxisSystem::EUpVector)((int32)Cima * (SinalDeCima < 0 ? -1 : 1)),
				(FbxAxisSystem::EFrontVector)((int32)FrenteAtual * (SinalDaFrente < 0 ? -1 : 1)),
				Atual.GetCoorSystem());

			// DeepConvertScene gira tambem as curvas de animacao, nao so a pose.
			FbxAxisSystem Conversor = Desejado;
			Conversor.DeepConvertScene(Cena);
		}

		// Unidade e escala viram um fator so: quantos centimetros vale uma
		// unidade do arquivo. Escala 2 quer o dobro do tamanho, entao divide.
		const double FatorDaUnidade = (Pedido.Opcoes->Unidade == EFofuxoUnidade::Metros) ? 100.0 : 1.0;
		const double Fator = FatorDaUnidade / FMath::Max(Pedido.Opcoes->Escala, UE_KINDA_SMALL_NUMBER);

		const FbxSystemUnit UnidadeDesejada(Fator);
		if (Cena->GetGlobalSettings().GetSystemUnit() != UnidadeDesejada)
		{
			UnidadeDesejada.ConvertScene(Cena);
		}
	}

	// WriteToFile fecha o documento sozinho.
	Exportador->WriteToFile(*Pedido.CaminhoDoArquivo);

	if (IFileManager::Get().FileSize(*Pedido.CaminhoDoArquivo) <= 0)
	{
		OutErro = FText::Format(
			LOCTEXT("ArquivoNaoSaiu", "O arquivo {0} nao foi escrito."),
			FText::FromString(Pedido.CaminhoDoArquivo));
		return false;
	}

	return true;
}

/**
 * Sai do caminho que o chamador pediu, e nao das opcoes -- quem chama nem sempre
 * e a janela: o comando de console passa um caminho absoluto direto.
 */
FString FofuxoCaminhoDoLote(const FString& Base, int32 Indice, int32 Total, const FString& NomeProprio)
{
	if (!NomeProprio.IsEmpty())
	{
		return FPaths::Combine(FPaths::GetPath(Base), NomeProprio + TEXT(".") + FPaths::GetExtension(Base));
	}

	if (Total <= 1)
	{
		return Base;
	}

	return FPaths::Combine(
		FPaths::GetPath(Base),
		FPaths::GetBaseFilename(Base) + FString::Printf(TEXT("_%02d."), Indice + 1) + FPaths::GetExtension(Base));
}

bool FFofuxoFbxWriter::Exportar(const FFofuxoExportPedido& Pedido, FText& OutErro)
{
	if (Pedido.Opcoes == nullptr)
	{
		OutErro = LOCTEXT("SemOpcoesLote", "Opcoes de export ausentes.");
		return false;
	}

	const int32 Total = Pedido.Animacoes.Num();

	// Sem animacao nenhuma sai um arquivo so, com a malha e o esqueleto -- e o
	// pedido de "so a malha". Lote nao se aplica: nao ha o que fatiar, e o 1
	// abaixo e so para o laco rodar uma vez com a lista vazia.
	const bool bSoAMalha = Total == 0;

	if (bSoAMalha && !Pedido.Opcoes->bExportarMalha)
	{
		OutErro = LOCTEXT("SemNada",
			"Sem animacao marcada e com \"Exportar a malha\" desligado nao sobra nada para escrever. "
			"Marque uma animacao, ou ligue a malha.");
		return false;
	}

	// 0 quer dizer "tudo junto", que e como o plugin nasceu e continua sendo o
	// que o Blender quer: um arquivo, varios takes nomeados.
	const int32 PorArquivo = bSoAMalha
		? 1
		: (Pedido.Opcoes->TakesPorArquivo > 0
			? FMath::Min(Pedido.Opcoes->TakesPorArquivo, Total)
			: Total);

	const int32 NumArquivos = bSoAMalha ? 1 : FMath::DivideAndRoundUp(Total, PorArquivo);

	// Uma etapa por take, mais uma por arquivo para a escrita. Sem isto o editor
	// atravessa a exportacao inteira sem pintar um quadro, e o Windows marca a
	// janela como "nao respondendo" -- que e exatamente a cara de um travamento.
	// So a malha nao tem take, mas tem a conversao da geometria: conta como uma.
	FScopedSlowTask Progresso(
		(bSoAMalha ? 1 : Total) + NumArquivos,
		FText::Format(LOCTEXT("Progresso", "{0} -- Exportar"), Fofuxo::NomeCurto()));

	Progresso.MakeDialog(/*bShowCancelButton*/ true);

	for (int32 Arquivo = 0; Arquivo < NumArquivos; ++Arquivo)
	{
		const int32 Primeira = Arquivo * PorArquivo;
		const int32 Quantas = FMath::Min(PorArquivo, Total - Primeira);

		// Cada lote e uma exportacao inteira e independente: documento novo,
		// cena nova, arquivo fechado no fim. E dai que vem o teto de memoria --
		// o que ja foi escrito nao fica mais na mao.
		FFofuxoExportPedido Parte = Pedido;
		if (Quantas > 0)
		{
			Parte.Animacoes = TArray<UAnimSequence*>(Pedido.Animacoes.GetData() + Primeira, Quantas);
		}
		else
		{
			Parte.Animacoes.Reset();
		}
		// Um take por arquivo: o arquivo leva o nome da animacao. Vale so quando
		// foi isso que se pediu -- o resto sobrando de um lote de 25 continua
		// numerado junto com os irmaos.
		const FString NomeProprio = (PorArquivo == 1 && Parte.Animacoes.Num() == 1 && Parte.Animacoes[0] != nullptr)
			? Parte.Animacoes[0]->GetName()
			: FString();

		Parte.CaminhoDoArquivo = FofuxoCaminhoDoLote(Pedido.CaminhoDoArquivo, Arquivo, NumArquivos, NomeProprio);

		if (!EscreverUmArquivo(Parte, Progresso, OutErro))
		{
			return false;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
