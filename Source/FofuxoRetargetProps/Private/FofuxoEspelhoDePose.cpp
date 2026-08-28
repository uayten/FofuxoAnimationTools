// Fofuxo's Exporter -- espelhar a pose de retarget

#include "FofuxoEspelhoDePose.h"

#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/ConfigCacheIni.h"
#include "ReferenceSkeleton.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"
#include "Toolkits/AssetEditorToolkit.h"

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoEspelho, Log, All);

namespace FofuxoEspelho
{
	static const TCHAR* SecaoIni = TEXT("FofuxoRetargetProps");
	static const TCHAR* ChaveIni = TEXT("EspelharPose");

	/** Quanto dois offsets podem diferir e ainda contarem como o mesmo. */
	static constexpr float Folga = 1.0e-6f;

	// -------------------------------------------------------------------------
	// Nomes
	// -------------------------------------------------------------------------

	/** Como o lado aparece escrito. A primeira e a esquerda, a segunda a direita. */
	struct FDupla
	{
		const TCHAR* Esquerda;
		const TCHAR* Direita;
	};

	static const FDupla Duplas[] =
	{
		{ TEXT("l"),    TEXT("r")     },
		{ TEXT("left"), TEXT("right") },
		{ TEXT("lt"),   TEXT("rt")    },
	};

	static bool ESeparador(const TCHAR C)
	{
		// Os dois pontos sao do Mixamo, que exporta "mixamorig:LeftArm".
		return C == TEXT('_') || C == TEXT('.') || C == TEXT('-') || C == TEXT(' ') || C == TEXT(':');
	}

	/**
	 * A palavra nova com a caixa da velha: "L" vira "R", "l" vira "r",
	 * "Left" vira "Right", "LEFT" vira "RIGHT".
	 */
	static FString ComACaixaDe(const FString& Modelo, const FString& Nova)
	{
		bool bTemMinuscula = false;
		bool bTemMaiuscula = false;

		for (const TCHAR C : Modelo)
		{
			bTemMinuscula |= FChar::IsLower(C);
			bTemMaiuscula |= FChar::IsUpper(C);
		}

		if (bTemMaiuscula && !bTemMinuscula)
		{
			return Nova.ToUpper();
		}

		if (bTemMinuscula && !bTemMaiuscula)
		{
			return Nova.ToLower();
		}

		// Misto, que na pratica e "Left": primeira maiuscula, resto minusculo.
		FString Resultado = Nova.ToLower();
		if (Resultado.Len() > 0)
		{
			Resultado[0] = FChar::ToUpper(Resultado[0]);
		}

		return Resultado;
	}

	/** O outro lado de um segmento que e so o lado ("l", "Left"), ou vazio. */
	static FString TrocarSegmento(const FString& Segmento)
	{
		for (const FDupla& Dupla : Duplas)
		{
			if (Segmento.Equals(Dupla.Esquerda, ESearchCase::IgnoreCase))
			{
				return ComACaixaDe(Segmento, Dupla.Direita);
			}

			if (Segmento.Equals(Dupla.Direita, ESearchCase::IgnoreCase))
			{
				return ComACaixaDe(Segmento, Dupla.Esquerda);
			}
		}

		return FString();
	}

	/**
	 * O outro lado de um segmento com o lado colado -- "HandL", "LHand",
	 * "LeftShoulder", "ShoulderLeft".
	 *
	 * Sem separador, a unica coisa que marca onde o lado comeca e a caixa: o
	 * pedaco do lado tem que abrir em maiuscula, e a vizinha tem que ser
	 * minuscula ou digito. Sem isso "Control" acabava em "Contror" e "Barrel"
	 * virava par de "Barrer".
	 */
	static FString TrocarColado(const FString& Segmento)
	{
		const int32 Tamanho = Segmento.Len();
		if (Tamanho < 2)
		{
			return FString();
		}

		// Prefixo: LHand, LeftShoulder. O que vem depois do lado tem que ser
		// maiuscula, ou "lowerarm" comecaria com o "l" da esquerda.
		for (const FDupla& Dupla : Duplas)
		{
			const TCHAR* Lados[] = { Dupla.Esquerda, Dupla.Direita };
			const TCHAR* Outros[] = { Dupla.Direita, Dupla.Esquerda };

			for (int32 Qual = 0; Qual < 2; ++Qual)
			{
				const int32 Quanto = FCString::Strlen(Lados[Qual]);

				if (Tamanho <= Quanto || !FChar::IsUpper(Segmento[0]))
				{
					continue;
				}

				if (Segmento.Left(Quanto).Equals(Lados[Qual], ESearchCase::IgnoreCase)
					&& FChar::IsUpper(Segmento[Quanto]))
				{
					return ComACaixaDe(Segmento.Left(Quanto), Outros[Qual]) + Segmento.Mid(Quanto);
				}
			}
		}

		// Sufixo: HandL, Spine01L, ShoulderLeft.
		for (const FDupla& Dupla : Duplas)
		{
			const TCHAR* Lados[] = { Dupla.Esquerda, Dupla.Direita };
			const TCHAR* Outros[] = { Dupla.Direita, Dupla.Esquerda };

			for (int32 Qual = 0; Qual < 2; ++Qual)
			{
				const int32 Quanto = FCString::Strlen(Lados[Qual]);
				const int32 Onde = Tamanho - Quanto;

				if (Onde < 1 || !FChar::IsUpper(Segmento[Onde]))
				{
					continue;
				}

				const TCHAR Vizinha = Segmento[Onde - 1];
				if (!FChar::IsLower(Vizinha) && !FChar::IsDigit(Vizinha))
				{
					continue;
				}

				if (Segmento.Mid(Onde).Equals(Lados[Qual], ESearchCase::IgnoreCase))
				{
					return Segmento.Left(Onde) + ComACaixaDe(Segmento.Mid(Onde), Outros[Qual]);
				}
			}
		}

		return FString();
	}

	/**
	 * O parceiro de um osso: o unico candidato que existe neste esqueleto.
	 *
	 * Dois candidatos validos e ambiguidade de verdade -- "L_arm_l" tanto pode
	 * ter parceiro "R_arm_l" quanto "L_arm_r" -- e adivinhar seria pior que nao
	 * espelhar.
	 */
	static FName Parceiro(const FName Osso, const FReferenceSkeleton& Esqueleto)
	{
		TArray<FString> Candidatos;
		FFofuxoEspelhoDePose::NomesEspelhados(Osso.ToString(), Candidatos);

		TArray<FName> Validos;
		for (const FString& Candidato : Candidatos)
		{
			const FName Nome(*Candidato);

			if (Nome != Osso && Esqueleto.FindBoneIndex(Nome) != INDEX_NONE)
			{
				Validos.AddUnique(Nome);
			}
		}

		if (Validos.Num() == 1)
		{
			return Validos[0];
		}

		if (Validos.Num() > 1)
		{
			UE_LOG(LogFofuxoEspelho, Verbose,
				TEXT("\"%s\" tem mais de um osso do outro lado possivel -- fica sem espelho."),
				*Osso.ToString());
		}

		return NAME_None;
	}

	// -------------------------------------------------------------------------
	// Geometria
	// -------------------------------------------------------------------------

	/**
	 * A mesma rotacao vista no espelho, com o plano passando pela origem e a
	 * normal no eixo dado.
	 *
	 * Refletir uma rotacao troca a mao, entao o que vale nao e refletir e sim
	 * conjugar pela reflexao: M R M. Em quaternio isso da manter a componente do
	 * eixo da normal e trocar o sinal das outras duas.
	 */
	static FQuat Espelhar(const FQuat& Rotacao, const int32 Eixo)
	{
		return FQuat(
			Eixo == 0 ? Rotacao.X : -Rotacao.X,
			Eixo == 1 ? Rotacao.Y : -Rotacao.Y,
			Eixo == 2 ? Rotacao.Z : -Rotacao.Z,
			Rotacao.W);
	}
}

void FFofuxoEspelhoDePose::NomesEspelhados(const FString& Nome, TArray<FString>& OutCandidatos)
{
	// Os pedacos entre separadores, guardados por posicao para o nome voltar a se
	// montar com os mesmos separadores que tinha.
	TArray<TPair<int32, int32>> Segmentos;

	int32 Inicio = 0;
	for (int32 Indice = 0; Indice <= Nome.Len(); ++Indice)
	{
		if (Indice == Nome.Len() || FofuxoEspelho::ESeparador(Nome[Indice]))
		{
			if (Indice > Inicio)
			{
				Segmentos.Emplace(Inicio, Indice - Inicio);
			}

			Inicio = Indice + 1;
		}
	}

	for (const TPair<int32, int32>& Segmento : Segmentos)
	{
		const FString Texto = Nome.Mid(Segmento.Key, Segmento.Value);

		FString Trocado = FofuxoEspelho::TrocarSegmento(Texto);
		if (Trocado.IsEmpty())
		{
			Trocado = FofuxoEspelho::TrocarColado(Texto);
		}

		if (!Trocado.IsEmpty())
		{
			OutCandidatos.Add(Nome.Left(Segmento.Key) + Trocado + Nome.Mid(Segmento.Key + Segmento.Value));
		}
	}
}

void FFofuxoEspelhoDePose::Iniciar()
{
	GConfig->GetBool(FofuxoEspelho::SecaoIni, FofuxoEspelho::ChaveIni, bLigado, GEditorPerProjectIni);

	// Todo quadro, e nao de meio em meio segundo como o dos anexos: aqui o
	// usuario esta com o gizmo na mao e o outro lado tem que andar junto.
	Ticker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FFofuxoEspelhoDePose::Tick), 0.0f);
}

void FFofuxoEspelhoDePose::Encerrar()
{
	if (Ticker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
		Ticker.Reset();
	}

	Vigiados.Reset();
}

void FFofuxoEspelhoDePose::Alternar()
{
	bLigado = !bLigado;
	GConfig->SetBool(FofuxoEspelho::SecaoIni, FofuxoEspelho::ChaveIni, bLigado, GEditorPerProjectIni);
}

void FFofuxoEspelhoDePose::Acompanhar(FIKRetargetEditor& Editor)
{
	const TSharedRef<FAssetEditorToolkit> Toolkit = StaticCastSharedRef<FAssetEditorToolkit>(Editor.AsShared());

	for (const FVigiado& Vigiado : Vigiados)
	{
		if (Vigiado.Toolkit.Pin() == Toolkit)
		{
			return;
		}
	}

	FVigiado Novo;
	Novo.Toolkit = Toolkit;
	Vigiados.Add(MoveTemp(Novo));
}

bool FFofuxoEspelhoDePose::Tick(float)
{
	Vigiados.RemoveAll([](const FVigiado& Vigiado) { return !Vigiado.Toolkit.IsValid(); });

	for (FVigiado& Vigiado : Vigiados)
	{
		const TSharedPtr<FAssetEditorToolkit> Toolkit = Vigiado.Toolkit.Pin();
		if (!Toolkit.IsValid() || Toolkit->GetEditorName() != FName("IKRetargetEditor"))
		{
			continue;
		}

		Conferir(Vigiado, *static_cast<FIKRetargetEditor*>(Toolkit.Get()));
	}

	return true;
}

void FFofuxoEspelhoDePose::Conferir(FVigiado& Vigiado, FIKRetargetEditor& Editor)
{
	const TSharedRef<FIKRetargetEditorController> Controlador = Editor.GetController();

	UIKRetargeterController* AssetController = Controlador->AssetController;
	if (AssetController == nullptr)
	{
		return;
	}

	const ERetargetSourceOrTarget Lado = Controlador->GetSourceOrTarget();
	USkeletalMesh* Malha = AssetController->GetPreviewMesh(Lado);
	const FName NomeDaPose = AssetController->GetCurrentRetargetPoseName(Lado);

	// Pelo Find, e nao pelo GetCurrentRetargetPose: aquele indexa o mapa direto e
	// estoura se o nome nao estiver la, o que acontece no meio de um Delete.
	FIKRetargetPose* Pose = AssetController->GetRetargetPoses(Lado).Find(NomeDaPose);

	if (Malha == nullptr || Pose == nullptr)
	{
		Vigiado.Instantaneo.Reset();
		Vigiado.bTemCache = false;
		return;
	}

	if (!Vigiado.bTemCache || Vigiado.Malha != Malha || Vigiado.Lado != static_cast<uint8>(Lado))
	{
		RefazerCache(Vigiado, Malha);
	}

	const TMap<FName, FQuat>& Atual = Pose->GetAllDeltaRotations();

	// Trocar de boneco, de pose ou de modo nao e edicao -- so vale sincronizar a
	// copia, senao a proxima rotacao seria comparada com a pose de outra pessoa.
	const bool bTrocou =
		Vigiado.Malha != Malha ||
		Vigiado.Lado != static_cast<uint8>(Lado) ||
		Vigiado.Pose != NomeDaPose;

	const bool bEditando = Controlador->GetRetargeterMode() == ERetargeterOutputMode::EditRetargetPose;

	if (bTrocou || !bLigado || !bEditando)
	{
		Vigiado.Malha = Malha;
		Vigiado.Lado = static_cast<uint8>(Lado);
		Vigiado.Pose = NomeDaPose;
		Vigiado.Instantaneo = Atual;
		return;
	}

	// O que mudou desde o quadro passado. Osso que sumiu do mapa voltou para a
	// identidade -- e um Reset, e tambem conta como mudanca.
	TSet<FName> Mudados;

	for (const TTuple<FName, FQuat>& Par : Atual)
	{
		const FQuat* Antes = Vigiado.Instantaneo.Find(Par.Key);
		const FQuat Velho = Antes != nullptr ? *Antes : FQuat::Identity;

		if (!Velho.Equals(Par.Value, FofuxoEspelho::Folga))
		{
			Mudados.Add(Par.Key);
		}
	}

	for (const TTuple<FName, FQuat>& Par : Vigiado.Instantaneo)
	{
		if (!Atual.Contains(Par.Key) && !Par.Value.Equals(FQuat::Identity, FofuxoEspelho::Folga))
		{
			Mudados.Add(Par.Key);
		}
	}

	if (Mudados.IsEmpty())
	{
		return;
	}

	const FReferenceSkeleton& Esqueleto = Malha->GetRefSkeleton();

	TArray<TTuple<FName, FQuat>> AEscrever;

	for (const FName& Osso : Mudados)
	{
		const FName OutroLado = Vigiado.Parceiros.FindRef(Osso);
		if (OutroLado.IsNone() || OutroLado == Osso)
		{
			continue;
		}

		// Os dois lados mudaram no mesmo quadro: foi de proposito -- gizmo com os
		// dois selecionados, Auto Align geral, Ctrl+Z. Espelhar aqui seria uma das
		// duas mudancas comendo a outra, e qual delas depende da ordem em que o
		// TSet devolveu.
		if (Mudados.Contains(OutroLado))
		{
			continue;
		}

		const int32 IndiceOsso = Esqueleto.FindBoneIndex(Osso);
		const int32 IndiceOutro = Esqueleto.FindBoneIndex(OutroLado);

		if (!Vigiado.RefComponente.IsValidIndex(IndiceOsso) || !Vigiado.RefComponente.IsValidIndex(IndiceOutro))
		{
			continue;
		}

		const FQuat* Achado = Atual.Find(Osso);
		const FQuat Delta = Achado != nullptr ? *Achado : FQuat::Identity;

		// O offset esta em espaco de osso, e os dois lados quase nunca tem os
		// eixos na mesma direcao -- trocar o sinal de dois componentes assim como
		// esta daria lixo. Entao: leva o delta para espaco de componente pelo ref
		// pose deste osso, espelha la, e traz de volta pelo ref pose do parceiro.
		const FQuat& RefOsso = Vigiado.RefComponente[IndiceOsso];
		const FQuat& RefOutro = Vigiado.RefComponente[IndiceOutro];

		const FQuat NoComponente = RefOsso * Delta * RefOsso.Inverse();
		const FQuat Espelhado = FofuxoEspelho::Espelhar(NoComponente, Vigiado.Eixo);
		const FQuat NoOutro = (RefOutro.Inverse() * Espelhado * RefOutro).GetNormalized();

		const FQuat* Tinha = Atual.Find(OutroLado);
		if (Tinha != nullptr && Tinha->Equals(NoOutro, FofuxoEspelho::Folga))
		{
			continue;
		}

		AEscrever.Emplace(OutroLado, NoOutro);
	}

	if (!AEscrever.IsEmpty())
	{
		// Durante o arrasto do gizmo a transacao ja esta aberta e o asset ja foi
		// marcado -- este Modify entra nela, e o Ctrl+Z desfaz os dois lados
		// juntos. Fora do arrasto ele so suja o pacote, que e o que se quer.
		if (UIKRetargeter* Asset = AssetController->GetAsset())
		{
			Asset->Modify();
		}

		for (const TTuple<FName, FQuat>& Par : AEscrever)
		{
			AssetController->SetRotationOffsetForRetargetPoseBone(Par.Key, Par.Value, Lado);
		}
	}

	// Depois das escritas, para o quadro seguinte nao ler o espelho como edicao.
	Vigiado.Instantaneo = Pose->GetAllDeltaRotations();
}

void FFofuxoEspelhoDePose::RefazerCache(FVigiado& Vigiado, USkeletalMesh* Malha)
{
	Vigiado.RefComponente.Reset();
	Vigiado.Parceiros.Reset();
	Vigiado.Eixo = 1;
	Vigiado.bTemCache = true;

	if (Malha == nullptr)
	{
		return;
	}

	const FReferenceSkeleton& Esqueleto = Malha->GetRefSkeleton();
	const TArray<FTransform>& Local = Esqueleto.GetRefBonePose();
	const int32 Quantos = Esqueleto.GetNum();

	// O ref pose em espaco de componente. A lista de ossos vem com o pai sempre
	// antes do filho, entao uma passada basta.
	TArray<FTransform> Componente;
	Componente.SetNum(Quantos);

	for (int32 Indice = 0; Indice < Quantos; ++Indice)
	{
		const int32 Pai = Esqueleto.GetParentIndex(Indice);
		Componente[Indice] = Pai == INDEX_NONE ? Local[Indice] : Local[Indice] * Componente[Pai];
	}

	Vigiado.RefComponente.SetNum(Quantos);
	for (int32 Indice = 0; Indice < Quantos; ++Indice)
	{
		Vigiado.RefComponente[Indice] = Componente[Indice].GetRotation().GetNormalized();
	}

	// Em que eixo os pares estao separados. Somado sobre todos os pares, o eixo
	// certo ganha de longe -- nao da para fixar em Y porque malha que veio do
	// Blender ou do Maya pode ter o boneco olhando para outro lado.
	FVector Separacao = FVector::ZeroVector;

	for (int32 Indice = 0; Indice < Quantos; ++Indice)
	{
		const FName Osso = Esqueleto.GetBoneName(Indice);
		const FName OutroLado = FofuxoEspelho::Parceiro(Osso, Esqueleto);

		Vigiado.Parceiros.Add(Osso, OutroLado);

		if (OutroLado.IsNone())
		{
			continue;
		}

		// So metade dos pares, senao cada um entra duas vezes.
		const int32 IndiceOutro = Esqueleto.FindBoneIndex(OutroLado);
		if (IndiceOutro <= Indice)
		{
			continue;
		}

		const FVector Diferenca = Componente[Indice].GetLocation() - Componente[IndiceOutro].GetLocation();
		Separacao += FVector(FMath::Abs(Diferenca.X), FMath::Abs(Diferenca.Y), FMath::Abs(Diferenca.Z));
	}

	// Empate, e o caso de nao ter par nenhum, fica em Y.
	if (Separacao.X > Separacao.Y && Separacao.X > Separacao.Z)
	{
		Vigiado.Eixo = 0;
	}
	else if (Separacao.Z > Separacao.X && Separacao.Z > Separacao.Y)
	{
		Vigiado.Eixo = 2;
	}

	int32 ComParceiro = 0;
	for (const TTuple<FName, FName>& Par : Vigiado.Parceiros)
	{
		ComParceiro += Par.Value.IsNone() ? 0 : 1;
	}

	static const TCHAR* NomeDoEixo[] = { TEXT("X"), TEXT("Y"), TEXT("Z") };

	UE_LOG(LogFofuxoEspelho, Display,
		TEXT("Espelho de %s: %d ossos de %d tem par, plano de espelho com normal em %s."),
		*Malha->GetName(), ComParceiro, Quantos, NomeDoEixo[Vigiado.Eixo]);
}
