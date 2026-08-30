// Fofuxo -- esticar ossos na pose de retarget

#include "FofuxoEsticarOssos.h"

#include "Engine/SkeletalMesh.h"
#include "Misc/ConfigCacheIni.h"
#include "ReferenceSkeleton.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "ToolMenu.h"
#include "ToolMenuContext.h"
#include "ToolMenuSection.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoEsticar, Log, All);

namespace FofuxoEsticar
{
	static const TCHAR* SecaoIni = TEXT("FofuxoRetargetProps");
	static const TCHAR* ChaveIni = TEXT("ModoDeEsticar");
	static const TCHAR* ChaveDoEixo = TEXT("EixoDoMundo");

	static bool bLidoDoIni = false;
	static EFofuxoModoDeEsticar ModoArmado = EFofuxoModoDeEsticar::Selecionados;

	static bool bEixoLidoDoIni = false;
	static EFofuxoEixoDoMundo EixoArmado = EFofuxoEixoDoMundo::MaisX;

	/** Os quatro, na ordem em que aparecem no menu. */
	static const EFofuxoModoDeEsticar Modos[] =
	{
		EFofuxoModoDeEsticar::Selecionados,
		EFofuxoModoDeEsticar::ComFilhos,
		EFofuxoModoDeEsticar::NoUltimo,
		EFofuxoModoDeEsticar::NoMundo,
	};

	/** Os seis, na ordem em que aparecem no menu. */
	static const EFofuxoEixoDoMundo Eixos[] =
	{
		EFofuxoEixoDoMundo::MaisX,
		EFofuxoEixoDoMundo::MenosX,
		EFofuxoEixoDoMundo::MaisY,
		EFofuxoEixoDoMundo::MenosY,
		EFofuxoEixoDoMundo::MaisZ,
		EFofuxoEixoDoMundo::MenosZ,
	};

	/**
	 * A orientacao que poe o X local -- a ponta do osso, na convencao da Unreal --
	 * em cima do eixo escolhido.
	 *
	 * Nao sao rotacoes quaisquer que levem um eixo no outro: cada uma e um giro de
	 * multiplo de 90 graus em torno de um eixo do mundo, entao os outros dois eixos
	 * do osso tambem caem em eixos do mundo. Uma FRotationMatrix::MakeFromX
	 * resolveria a ponta e deixaria o giro em torno dela por conta da biblioteca --
	 * e o giro em torno da propria arma e justamente o que nao pode ficar solto.
	 */
	static FQuat OrientacaoDoEixo(const EFofuxoEixoDoMundo Doque)
	{
		// FRotator e (Pitch, Yaw, Roll): o pitch gira em torno do Y do mundo, o yaw
		// em torno do Z.
		switch (Doque)
		{
		case EFofuxoEixoDoMundo::MenosX: return FRotator(0.0, 180.0, 0.0).Quaternion();
		case EFofuxoEixoDoMundo::MaisY:  return FRotator(0.0, 90.0, 0.0).Quaternion();
		case EFofuxoEixoDoMundo::MenosY: return FRotator(0.0, -90.0, 0.0).Quaternion();
		case EFofuxoEixoDoMundo::MaisZ:  return FRotator(90.0, 0.0, 0.0).Quaternion();
		case EFofuxoEixoDoMundo::MenosZ: return FRotator(-90.0, 0.0, 0.0).Quaternion();
		default:                         return FQuat::Identity;
		}
	}

	/** Quantos ossos o modo precisa ter selecionados para fazer alguma coisa. */
	static int32 MinimoDeOssos(const EFofuxoModoDeEsticar Doque)
	{
		// Alinhar um osso sozinho na orientacao dele mesmo nao muda nada, e botao
		// que aceita o clique sem fazer nada e pior que botao apagado.
		return Doque == EFofuxoModoDeEsticar::NoUltimo ? 2 : 1;
	}

	/**
	 * O modo joga a selecao inteira numa orientacao unica, em espaco de componente?
	 *
	 * Os dois que fazem isso -- NoUltimo e NoMundo -- sao a mesma conta; muda so de
	 * onde sai a orientacao alvo.
	 */
	static bool TemOrientacaoUnica(const EFofuxoModoDeEsticar Doque)
	{
		return Doque == EFofuxoModoDeEsticar::NoUltimo || Doque == EFofuxoModoDeEsticar::NoMundo;
	}

	/** As pecas do editor que toda operacao daqui precisa. */
	struct FAlvo
	{
		UIKRetargeterController* AssetController = nullptr;
		USkeletalMesh* Malha = nullptr;
		ERetargetSourceOrTarget Lado = ERetargetSourceOrTarget::Source;

		bool Servem() const { return AssetController != nullptr && Malha != nullptr; }
	};

	static FAlvo AlvoDoEditor(FIKRetargetEditor& Editor)
	{
		const TSharedRef<FIKRetargetEditorController> Controlador = Editor.GetController();

		FAlvo Alvo;
		Alvo.AssetController = Controlador->AssetController;
		Alvo.Lado = Controlador->GetSourceOrTarget();

		if (Alvo.AssetController != nullptr)
		{
			Alvo.Malha = Alvo.AssetController->GetPreviewMesh(Alvo.Lado);
		}

		return Alvo;
	}

	/** Os selecionados que existem neste esqueleto, na ordem em que foram clicados. */
	static void Selecionados(
		const FIKRetargetEditorController& Controlador,
		const FReferenceSkeleton& Esqueleto,
		TArray<int32>& OutIndices)
	{
		for (const FName& Osso : Controlador.GetSelectedBones())
		{
			const int32 Indice = Esqueleto.FindBoneIndex(Osso);
			if (Indice != INDEX_NONE)
			{
				OutIndices.AddUnique(Indice);
			}
		}
	}

	/** Junta aos indices tudo que desce deles. */
	static void ComOsFilhos(const FReferenceSkeleton& Esqueleto, TArray<int32>& Indices)
	{
		// Uma passada so, para frente: na lista do esqueleto o pai vem sempre
		// antes do filho, entao quando a vez do neto chega o filho ja entrou.
		const int32 Quantos = Esqueleto.GetNum();

		for (int32 Indice = 0; Indice < Quantos; ++Indice)
		{
			const int32 Pai = Esqueleto.GetParentIndex(Indice);
			if (Pai != INDEX_NONE && Indices.Contains(Pai))
			{
				Indices.AddUnique(Indice);
			}
		}
	}

	/** O delta que este osso tem hoje na pose corrente, ou identidade. */
	static FQuat DeltaDe(const TMap<FName, FQuat>& Deltas, const FName Osso)
	{
		const FQuat* Achado = Deltas.Find(Osso);
		return Achado != nullptr ? *Achado : FQuat::Identity;
	}
}

FIKRetargetEditor* FFofuxoEsticarOssos::EditorDoContexto(const FToolMenuContext& Contexto)
{
	const UAssetEditorToolkitMenuContext* DoEditor = Contexto.FindContext<UAssetEditorToolkitMenuContext>();
	if (DoEditor == nullptr)
	{
		return nullptr;
	}

	// A barra do editor de retarget e o unico lugar onde esta entrada foi posta,
	// mas o contexto e generico e nada impede outra barra de herdar dela.
	const TSharedPtr<FAssetEditorToolkit> Toolkit = DoEditor->Toolkit.Pin();
	if (!Toolkit.IsValid() || Toolkit->GetEditorName() != FName("IKRetargetEditor"))
	{
		return nullptr;
	}

	return static_cast<FIKRetargetEditor*>(Toolkit.Get());
}

EFofuxoModoDeEsticar FFofuxoEsticarOssos::Modo()
{
	if (!FofuxoEsticar::bLidoDoIni)
	{
		FofuxoEsticar::bLidoDoIni = true;

		int32 Guardado = static_cast<int32>(FofuxoEsticar::ModoArmado);
		GConfig->GetInt(FofuxoEsticar::SecaoIni, FofuxoEsticar::ChaveIni, Guardado, GEditorPerProjectIni);

		// Ini escrito por uma versao com mais modos que esta nao pode armar um modo
		// que nao existe -- cairia no default: do switch e o botao nao faria nada.
		if (Guardado >= 0 && Guardado < static_cast<int32>(UE_ARRAY_COUNT(FofuxoEsticar::Modos)))
		{
			FofuxoEsticar::ModoArmado = static_cast<EFofuxoModoDeEsticar>(Guardado);
		}
	}

	return FofuxoEsticar::ModoArmado;
}

void FFofuxoEsticarOssos::EscolherModo(const EFofuxoModoDeEsticar Novo)
{
	// A leitura preguicosa tem que acontecer antes: sem isto a primeira chamada a
	// Modo() depois desta iria ao ini e desfaria a escolha.
	Modo();

	FofuxoEsticar::ModoArmado = Novo;

	GConfig->SetInt(FofuxoEsticar::SecaoIni, FofuxoEsticar::ChaveIni,
		static_cast<int32>(Novo), GEditorPerProjectIni);
}

EFofuxoEixoDoMundo FFofuxoEsticarOssos::Eixo()
{
	if (!FofuxoEsticar::bEixoLidoDoIni)
	{
		FofuxoEsticar::bEixoLidoDoIni = true;

		int32 Guardado = static_cast<int32>(FofuxoEsticar::EixoArmado);
		GConfig->GetInt(FofuxoEsticar::SecaoIni, FofuxoEsticar::ChaveDoEixo, Guardado, GEditorPerProjectIni);

		if (Guardado >= 0 && Guardado < static_cast<int32>(UE_ARRAY_COUNT(FofuxoEsticar::Eixos)))
		{
			FofuxoEsticar::EixoArmado = static_cast<EFofuxoEixoDoMundo>(Guardado);
		}
	}

	return FofuxoEsticar::EixoArmado;
}

void FFofuxoEsticarOssos::EscolherEixo(const EFofuxoEixoDoMundo Novo)
{
	// Mesma armadilha do modo: sem forcar a leitura antes, a primeira chamada a
	// Eixo() depois desta iria ao ini e desfaria a escolha.
	Eixo();

	FofuxoEsticar::EixoArmado = Novo;

	GConfig->SetInt(FofuxoEsticar::SecaoIni, FofuxoEsticar::ChaveDoEixo,
		static_cast<int32>(Novo), GEditorPerProjectIni);

	// Escolher eixo e dizer que se quer alinhar no mundo. Obrigar a clicar duas
	// vezes -- o modo e depois o eixo -- so serviria para o clique no eixo nao
	// fazer nada visivel.
	EscolherModo(EFofuxoModoDeEsticar::NoMundo);
}

FText FFofuxoEsticarOssos::NomeDoEixo(const EFofuxoEixoDoMundo Doque)
{
	switch (Doque)
	{
	case EFofuxoEixoDoMundo::MenosX: return LOCTEXT("EixoMenosX", "-X");
	case EFofuxoEixoDoMundo::MaisY:  return LOCTEXT("EixoMaisY", "+Y");
	case EFofuxoEixoDoMundo::MenosY: return LOCTEXT("EixoMenosY", "-Y");
	case EFofuxoEixoDoMundo::MaisZ:  return LOCTEXT("EixoMaisZ", "+Z");
	case EFofuxoEixoDoMundo::MenosZ: return LOCTEXT("EixoMenosZ", "-Z");
	default:                         return LOCTEXT("EixoMaisX", "+X");
	}
}

FText FFofuxoEsticarOssos::Rotulo(const EFofuxoModoDeEsticar Doque)
{
	switch (Doque)
	{
	case EFofuxoModoDeEsticar::ComFilhos:
		return LOCTEXT("EsticarComFilhos", "Esticar com filhos");

	case EFofuxoModoDeEsticar::NoUltimo:
		return LOCTEXT("EsticarNoUltimo", "Alinhar no ultimo");

	case EFofuxoModoDeEsticar::NoMundo:
		return FText::Format(LOCTEXT("EsticarNoMundo", "Alinhar no {0}"), NomeDoEixo(Eixo()));

	default:
		return LOCTEXT("Esticar", "Esticar");
	}
}

FText FFofuxoEsticarOssos::Dica(const EFofuxoModoDeEsticar Doque)
{
	switch (Doque)
	{
	case EFofuxoModoDeEsticar::ComFilhos:
		return LOCTEXT("EsticarComFilhosTip",
			"O mesmo esticar, levando junto tudo que desce dos ossos selecionados: com a mao "
			"selecionada, sai a mao inteira aberta.");

	case EFofuxoModoDeEsticar::NoUltimo:
		return LOCTEXT("EsticarNoUltimoTip",
			"Poe todos os ossos selecionados apontando para o mesmo lado que o ultimo que voce "
			"clicou. Nao sao os eixos do pai de cada um, e sim uma orientacao unica -- a daquele osso.\n\n"
			"Serve para a cadeia que voce ja consertou na ponta: acertou a ultima falange no gizmo, "
			"selecione as outras, clique nela por ultimo, e as tres ficam iguais.\n\n"
			"O ultimo e o ultimo clicado com Ctrl no visor, ou o de baixo na lista da hierarquia. "
			"Nao mexe na posicao de ninguem.");

	case EFofuxoModoDeEsticar::NoMundo:
		return FText::Format(LOCTEXT("EsticarNoMundoTip",
			"Poe os eixos do osso em cima dos eixos do mundo, com a ponta dele apontando para {0}. "
			"Nao olha para o pai nem para o resto da pose.\n\n"
			"E' para arma. Um osso de arma so serve se estiver na mesma orientacao no personagem e na "
			"arma, e \"mesma\" precisa de uma referencia que nao seja nenhum dos dois -- senao voce fica "
			"ajustando um contra o outro. Alinhe no mundo o osso da mao aqui e o osso raiz da arma no "
			"retargeter dela, e os dois batem sem medir nada, ja no Running Retarget. A proxima arma "
			"entra alinhada de graca. Qual eixo voce escolhe nao muda nada, desde que seja o mesmo nos "
			"dois assets.\n\n"
			"E' o osso deitado apontando para +Y do Blender: la o osso e desenhado ao longo do proprio "
			"Y, aqui a convencao da Unreal poe o comprimento no X. Se o seu esqueleto usar outro eixo "
			"como comprimento, o nome do eixo nao descreve para onde o osso vai apontar -- mas as seis "
			"escolhas continuam sendo seis orientacoes fixas, e a que parecer certa serve.\n\n"
			"Nao mexe na posicao."),
			NomeDoEixo(Eixo()));

	default:
		return LOCTEXT("EsticarTip",
			"Poe os ossos selecionados com os mesmos eixos do pai de cada um -- numa cadeia inteira, "
			"e o mesmo que estica-la. Feito para dedo: em vez de acertar falange por falange no gizmo, "
			"selecione as tres e estique.\n\n"
			"O mesmo que o Alt+R do Blender: deixa a rotacao local em zero, com os eixos do osso caindo "
			"em cima dos eixos do pai. Nao mexe na posicao -- o osso continua nascendo de onde nascia.");
	}
}

FSlateIcon FFofuxoEsticarOssos::Icone(const EFofuxoModoDeEsticar Doque)
{
	// O icone das tangentes retas serve para os dois esticares; os dois alinhares
	// sao outra operacao e ganham os seus, senao so o rotulo distingue os modos na
	// barra.
	switch (Doque)
	{
	case EFofuxoModoDeEsticar::NoUltimo:
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Adjust");

	case EFofuxoModoDeEsticar::NoMundo:
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.World");

	default:
		return FSlateIcon(FAppStyle::GetAppStyleSetName(), "CurveEditor.StraightenTangents");
	}
}

bool FFofuxoEsticarOssos::Pode(const FToolMenuContext& Contexto)
{
	FIKRetargetEditor* Editor = EditorDoContexto(Contexto);
	if (Editor == nullptr)
	{
		return false;
	}

	const TSharedRef<FIKRetargetEditorController> Controlador = Editor->GetController();

	return Controlador->GetRetargeterMode() == ERetargeterOutputMode::EditRetargetPose
		&& Controlador->GetSelectedBones().Num() >= FofuxoEsticar::MinimoDeOssos(Modo());
}

void FFofuxoEsticarOssos::Esticar(const FToolMenuContext& Contexto)
{
	FIKRetargetEditor* Editor = EditorDoContexto(Contexto);
	if (Editor == nullptr)
	{
		return;
	}

	const TSharedRef<FIKRetargetEditorController> Controlador = Editor->GetController();

	const FofuxoEsticar::FAlvo Alvo = FofuxoEsticar::AlvoDoEditor(*Editor);
	if (!Alvo.Servem())
	{
		return;
	}

	const FReferenceSkeleton& Esqueleto = Alvo.Malha->GetRefSkeleton();
	const TArray<FTransform>& Local = Esqueleto.GetRefBonePose();

	TArray<int32> Alvos;
	FofuxoEsticar::Selecionados(*Controlador, Esqueleto, Alvos);

	const EFofuxoModoDeEsticar Armado = Modo();

	if (Armado == EFofuxoModoDeEsticar::ComFilhos)
	{
		FofuxoEsticar::ComOsFilhos(Esqueleto, Alvos);
	}

	if (Alvos.Num() < FofuxoEsticar::MinimoDeOssos(Armado))
	{
		return;
	}

	// O que vai ser escrito, decidido inteiro antes de a primeira escrita sair: nos
	// modos de orientacao unica a conta de um osso usa a pose ja corrigida do pai
	// dele, e misturar leitura com escrita no mapa da pose seria ler metade de cada.
	TArray<TTuple<FName, FQuat>> AEscrever;
	int32 SemPai = 0;

	if (FofuxoEsticar::TemOrientacaoUnica(Armado))
	{
		const int32 Quantos = Esqueleto.GetNum();

		const TMap<FName, FQuat>& Deltas =
			Alvo.AssetController->GetCurrentRetargetPose(Alvo.Lado).GetAllDeltaRotations();

		TArray<FQuat> Componente;
		Componente.SetNum(Quantos);

		// No mundo o alvo e uma constante, e nao ha o que medir. No ultimo, o alvo e a
		// orientacao que um osso *tem hoje*, e ai a pose corrente precisa ser levada
		// para espaco de componente antes de qualquer escrita. O pai vem antes do
		// filho na lista, entao uma passada basta.
		FQuat Orientacao = FofuxoEsticar::OrientacaoDoEixo(Eixo());

		if (Armado == EFofuxoModoDeEsticar::NoUltimo)
		{
			for (int32 Indice = 0; Indice < Quantos; ++Indice)
			{
				const FQuat Rot = Local[Indice].GetRotation()
					* FofuxoEsticar::DeltaDe(Deltas, Esqueleto.GetBoneName(Indice));

				const int32 Pai = Esqueleto.GetParentIndex(Indice);
				Componente[Indice] = (Pai == INDEX_NONE ? Rot : Componente[Pai] * Rot).GetNormalized();
			}

			// A ordem de Alvos e a ordem em que os ossos foram clicados:
			// EditBoneSelection so faz AddUnique no fim da lista.
			Orientacao = Componente[Alvos.Last()];
		}

		// Segunda passada, agora corrigindo. Um osso selecionado pode ser filho de
		// outro osso selecionado, e ai o pai ja virou quando a vez do filho chega --
		// por isso o espaco de componente e refeito no caminho, e por isso o proprio
		// osso de referencia entra na conta: se um ancestral dele virou, ele saiu do
		// lugar junto e precisa voltar.
		const TSet<int32> Escolhidos(Alvos);

		for (int32 Indice = 0; Indice < Quantos; ++Indice)
		{
			const int32 Pai = Esqueleto.GetParentIndex(Indice);
			const FQuat DoPai = Pai == INDEX_NONE ? FQuat::Identity : Componente[Pai];

			if (!Escolhidos.Contains(Indice))
			{
				const FQuat Rot = Local[Indice].GetRotation()
					* FofuxoEsticar::DeltaDe(Deltas, Esqueleto.GetBoneName(Indice));

				Componente[Indice] = (DoPai * Rot).GetNormalized();
				continue;
			}

			// LocalRot = RefLocal.Rot * Delta, e o que se quer e DoPai * LocalRot == Orientacao.
			const FQuat Novo =
				(Local[Indice].GetRotation().Inverse() * DoPai.Inverse() * Orientacao).GetNormalized();

			AEscrever.Emplace(Esqueleto.GetBoneName(Indice), Novo);
			Componente[Indice] = Orientacao;
		}
	}
	else
	{
		for (const int32 Indice : Alvos)
		{
			if (Esqueleto.GetParentIndex(Indice) == INDEX_NONE)
			{
				// Osso sem pai nao tem com que se alinhar: o "pai" dele e o proprio
				// componente, e zerar ali deitaria o boneco inteiro.
				++SemPai;
				continue;
			}

			AEscrever.Emplace(
				Esqueleto.GetBoneName(Indice),
				Local[Indice].GetRotation().Inverse().GetNormalized());
		}
	}

	if (AEscrever.IsEmpty())
	{
		return;
	}

	const FScopedTransaction Transacao(LOCTEXT("EsticarTransacao", "Esticar ossos"));

	if (UIKRetargeter* Asset = Alvo.AssetController->GetAsset())
	{
		Asset->Modify();
	}

	// O retargeter reinicializa uma vez, no fim do escopo -- e nao uma vez por
	// osso, que numa mao inteira seriam quinze.
	const FScopedReinitializeIKRetargeter Reinicializar(Alvo.AssetController);

	for (const TTuple<FName, FQuat>& Par : AEscrever)
	{
		Alvo.AssetController->SetRotationOffsetForRetargetPoseBone(Par.Key, Par.Value, Alvo.Lado);
	}

	UE_LOG(LogFofuxoEsticar, Display,
		TEXT("%s: %d ossos de %s.%s"),
		*Rotulo(Armado).ToString(),
		AEscrever.Num(),
		*Alvo.Malha->GetName(),
		SemPai > 0 ? TEXT(" A raiz ficou de fora, nao tem pai com que se alinhar.") : TEXT(""));
}

bool FFofuxoEsticarOssos::DeltaParaOMundo(
	UIKRetargeterController& Controlador,
	const ERetargetSourceOrTarget Lado,
	const FName Osso,
	const EFofuxoEixoDoMundo Eixo,
	FQuat& OutDelta)
{
	USkeletalMesh* Malha = Controlador.GetPreviewMesh(Lado);
	if (Malha == nullptr)
	{
		return false;
	}

	const FReferenceSkeleton& Esqueleto = Malha->GetRefSkeleton();

	const int32 Indice = Esqueleto.FindBoneIndex(Osso);
	if (Indice == INDEX_NONE)
	{
		return false;
	}

	const TArray<FTransform>& Local = Esqueleto.GetRefBonePose();
	const TMap<FName, FQuat>& Deltas =
		Controlador.GetCurrentRetargetPose(Lado).GetAllDeltaRotations();

	// A orientacao do pai na pose de agora. Subindo ate a raiz e voltando, porque o
	// que importa e a cadeia deste osso -- o resto do esqueleto nao entra na conta.
	TArray<int32> Cadeia;
	for (int32 Subindo = Esqueleto.GetParentIndex(Indice); Subindo != INDEX_NONE;
		Subindo = Esqueleto.GetParentIndex(Subindo))
	{
		Cadeia.Add(Subindo);
	}

	FQuat DoPai = FQuat::Identity;
	for (int32 Passo = Cadeia.Num() - 1; Passo >= 0; --Passo)
	{
		const int32 Quem = Cadeia[Passo];

		const FQuat Rot = Local[Quem].GetRotation()
			* FofuxoEsticar::DeltaDe(Deltas, Esqueleto.GetBoneName(Quem));

		DoPai = (DoPai * Rot).GetNormalized();
	}

	// LocalRot = RefLocal.Rot * Delta, e o que se quer e DoPai * LocalRot == Orientacao.
	OutDelta = (Local[Indice].GetRotation().Inverse()
		* DoPai.Inverse()
		* FofuxoEsticar::OrientacaoDoEixo(Eixo)).GetNormalized();

	return true;
}

void FFofuxoEsticarOssos::MontarMenuDeModos(UToolMenu* Menu)
{
	FToolMenuSection& Secao = Menu->FindOrAddSection(
		"FofuxoEsticarModos", LOCTEXT("EsticarModos", "O que o botao faz"));

	for (const EFofuxoModoDeEsticar Doque : FofuxoEsticar::Modos)
	{
		Secao.AddMenuEntry(
			*FString::Printf(TEXT("FofuxoEsticarModo%d"), static_cast<int32>(Doque)),
			Rotulo(Doque),
			Dica(Doque),
			Icone(Doque),
			FToolUIActionChoice(FToolUIAction(
				FToolMenuExecuteAction::CreateLambda(
					[Doque](const FToolMenuContext&) { FFofuxoEsticarOssos::EscolherModo(Doque); }),
				FToolMenuCanExecuteAction(),
				FToolMenuGetActionCheckState::CreateLambda([Doque](const FToolMenuContext&)
				{
					return FFofuxoEsticarOssos::Modo() == Doque
						? ECheckBoxState::Checked
						: ECheckBoxState::Unchecked;
				}))),
			EUserInterfaceActionType::RadioButton);
	}

	// O eixo em secao propria, e nao num submenu pendurado no item do modo: assim
	// da para ver qual esta escolhido sem passar o mouse em nada, e clicar um deles
	// ja arma o NoMundo -- e o unico modo que le isto.
	FToolMenuSection& DosEixos = Menu->FindOrAddSection(
		"FofuxoEixosDoMundo", LOCTEXT("EsticarEixos", "Alinhar no mundo: para onde a ponta aponta"));

	for (const EFofuxoEixoDoMundo Doque : FofuxoEsticar::Eixos)
	{
		DosEixos.AddMenuEntry(
			*FString::Printf(TEXT("FofuxoEixoDoMundo%d"), static_cast<int32>(Doque)),
			NomeDoEixo(Doque),
			FText::Format(
				LOCTEXT("EsticarEixoTip",
					"O osso fica com os eixos nos eixos do mundo e a ponta em {0}. Escolher isto arma o "
					"Alinhar no mundo."),
				NomeDoEixo(Doque)),
			FSlateIcon(),
			FToolUIActionChoice(FToolUIAction(
				FToolMenuExecuteAction::CreateLambda(
					[Doque](const FToolMenuContext&) { FFofuxoEsticarOssos::EscolherEixo(Doque); }),
				FToolMenuCanExecuteAction(),
				FToolMenuGetActionCheckState::CreateLambda([Doque](const FToolMenuContext&)
				{
					// Marcado so quando o modo tambem esta armado: com outro modo na
					// barra, um radio aceso aqui diria que o clique vai alinhar no
					// mundo, e nao vai.
					return FFofuxoEsticarOssos::Modo() == EFofuxoModoDeEsticar::NoMundo
						&& FFofuxoEsticarOssos::Eixo() == Doque
						? ECheckBoxState::Checked
						: ECheckBoxState::Unchecked;
				}))),
			EUserInterfaceActionType::RadioButton);
	}
}

#undef LOCTEXT_NAMESPACE
