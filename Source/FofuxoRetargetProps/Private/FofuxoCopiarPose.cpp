// Fofuxo's Exporter -- copiar a pose de retarget de outro retargeter

#include "FofuxoCopiarPose.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
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

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoCopiarPose, Log, All);

namespace FofuxoCopiar
{
	/** Quanto um delta pode diferir da identidade e ainda contar como nao posado. */
	static constexpr float Folga = 1.0e-6f;

	static const ERetargetSourceOrTarget Lados[] =
	{
		ERetargetSourceOrTarget::Source,
		ERetargetSourceOrTarget::Target,
	};

	static FText NomeDoLado(const ERetargetSourceOrTarget Lado)
	{
		return Lado == ERetargetSourceOrTarget::Source
			? LOCTEXT("LadoFonte", "Fonte")
			: LOCTEXT("LadoAlvo", "Alvo");
	}

	/** Para onde a pose vai: o lado que o editor esta editando agora. */
	struct FDestino
	{
		UIKRetargeterController* AssetController = nullptr;
		UIKRetargeter* Asset = nullptr;
		USkeletalMesh* Malha = nullptr;
		ERetargetSourceOrTarget Lado = ERetargetSourceOrTarget::Source;
		FName Pose;

		bool Serve() const { return AssetController != nullptr && Asset != nullptr && Malha != nullptr; }
	};

	static FDestino DestinoDoEditor(FIKRetargetEditor& Editor)
	{
		const TSharedRef<FIKRetargetEditorController> Controlador = Editor.GetController();

		FDestino Destino;
		Destino.AssetController = Controlador->AssetController;
		Destino.Lado = Controlador->GetSourceOrTarget();

		if (Destino.AssetController != nullptr)
		{
			Destino.Asset = Destino.AssetController->GetAsset();
			Destino.Malha = Destino.AssetController->GetPreviewMesh(Destino.Lado);
			Destino.Pose = Destino.AssetController->GetCurrentRetargetPoseName(Destino.Lado);
		}

		return Destino;
	}

	/** Os retargeters do projeto, em ordem alfabetica, sem carregar nenhum. */
	static void Retargeters(TArray<FAssetData>& OutAssets)
	{
		IAssetRegistry& Registro =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		Registro.GetAssetsByClass(UIKRetargeter::StaticClass()->GetClassPathName(), OutAssets, /*bSearchSubClasses*/ true);

		OutAssets.Sort([](const FAssetData& A, const FAssetData& B)
		{
			return A.AssetName.LexicalLess(B.AssetName);
		});
	}

	/** Quantos ossos desta pose estao de fato posados. */
	static int32 QuantosPosados(const FIKRetargetPose& Pose)
	{
		int32 Quantos = 0;

		for (const TTuple<FName, FQuat>& Par : Pose.GetAllDeltaRotations())
		{
			if (!Par.Value.Equals(FQuat::Identity, Folga))
			{
				++Quantos;
			}
		}

		return Quantos;
	}

	/** O que a copia vai fazer, contado antes de escrever qualquer coisa. */
	struct FConferencia
	{
		// Osso e delta, so os que existem no esqueleto do destino.
		TArray<TTuple<FName, FQuat>> Batem;

		// Posados na origem que este esqueleto nao tem.
		TArray<FName> SemOsso;

		// Posados aqui que la nao estao, e que a copia devolve ao ref pose.
		TArray<FName> Zeram;

		FVector Pelvis = FVector::ZeroVector;
	};

	static FConferencia Conferir(
		const FIKRetargetPose& Origem,
		const FIKRetargetPose& Atual,
		const FReferenceSkeleton& Esqueleto)
	{
		FConferencia Conta;
		Conta.Pelvis = Origem.GetRootTranslationDelta();

		TSet<FName> Chegando;

		for (const TTuple<FName, FQuat>& Par : Origem.GetAllDeltaRotations())
		{
			if (Par.Value.Equals(FQuat::Identity, Folga))
			{
				// Osso que a origem lista mas nao poso. Copiar identidade so encheria
				// o mapa do destino de entradas que nao fazem nada.
				continue;
			}

			if (Esqueleto.FindBoneIndex(Par.Key) == INDEX_NONE)
			{
				Conta.SemOsso.Add(Par.Key);
				continue;
			}

			Conta.Batem.Emplace(Par.Key, Par.Value);
			Chegando.Add(Par.Key);
		}

		for (const TTuple<FName, FQuat>& Par : Atual.GetAllDeltaRotations())
		{
			if (!Par.Value.Equals(FQuat::Identity, Folga) && !Chegando.Contains(Par.Key))
			{
				Conta.Zeram.Add(Par.Key);
			}
		}

		Conta.Batem.Sort([](const TTuple<FName, FQuat>& A, const TTuple<FName, FQuat>& B)
		{
			return A.Key.LexicalLess(B.Key);
		});

		Conta.SemOsso.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		Conta.Zeram.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });

		return Conta;
	}

	static FText MontarPergunta(
		const FConferencia& Conta,
		const FDestino& Destino,
		const UIKRetargeter& Origem,
		const ERetargetSourceOrTarget LadoDaOrigem,
		const FName PoseDaOrigem)
	{
		FText Texto = FText::Format(
			LOCTEXT("CopiarPergunta",
				"Copiar a pose \"{0}\" do lado {1} de {2} para a pose \"{3}\" do lado {4} de {5}?\n\n"
				"{6} ossos posados la existem neste esqueleto e vem por nome."),
			FText::FromName(PoseDaOrigem),
			NomeDoLado(LadoDaOrigem),
			FText::FromString(Origem.GetName()),
			FText::FromName(Destino.Pose),
			NomeDoLado(Destino.Lado),
			FText::FromString(Destino.Asset->GetName()),
			FText::AsNumber(Conta.Batem.Num()));

		if (Conta.SemOsso.Num() > 0)
		{
			Texto = FText::Format(
				LOCTEXT("CopiarSemOsso",
					"{0}\n{1} nao existem em {2} e ficam de fora (os nomes vao para o Output Log)."),
				Texto,
				FText::AsNumber(Conta.SemOsso.Num()),
				FText::FromString(Destino.Malha->GetName()));
		}

		if (Conta.Zeram.Num() > 0)
		{
			Texto = FText::Format(
				LOCTEXT("CopiarZeram",
					"{0}\n{1} ossos que voce posou aqui nao estao posados la e voltam para o ref pose "
					"-- copiar e ficar igual, nao somar."),
				Texto,
				FText::AsNumber(Conta.Zeram.Num()));
		}

		if (!Conta.Pelvis.IsNearlyZero())
		{
			Texto = FText::Format(
				LOCTEXT("CopiarPelvis",
					"{0}\n\nO deslocamento do pelvis vem junto ({1}). Esse e o unico valor em centimetros "
					"da copia: entre bonecos de tamanhos diferentes, confira depois."),
				Texto,
				FText::FromString(Conta.Pelvis.ToCompactString()));
		}

		return FText::Format(LOCTEXT("CopiarFecho", "{0}\n\nUm Ctrl+Z desfaz tudo de uma vez."), Texto);
	}
}

FIKRetargetEditor* FFofuxoCopiarPose::EditorDoContexto(const FToolMenuContext& Contexto)
{
	const UAssetEditorToolkitMenuContext* DoEditor = Contexto.FindContext<UAssetEditorToolkitMenuContext>();
	if (DoEditor == nullptr)
	{
		return nullptr;
	}

	const TSharedPtr<FAssetEditorToolkit> Toolkit = DoEditor->Toolkit.Pin();
	if (!Toolkit.IsValid() || Toolkit->GetEditorName() != FName("IKRetargetEditor"))
	{
		return nullptr;
	}

	return static_cast<FIKRetargetEditor*>(Toolkit.Get());
}

bool FFofuxoCopiarPose::Pode(const FToolMenuContext& Contexto)
{
	FIKRetargetEditor* Editor = EditorDoContexto(Contexto);

	return Editor != nullptr && FofuxoCopiar::DestinoDoEditor(*Editor).Serve();
}

void FFofuxoCopiarPose::MontarMenu(UToolMenu* Menu)
{
	FIKRetargetEditor* Editor = EditorDoContexto(Menu->Context);
	if (Editor == nullptr)
	{
		return;
	}

	const FofuxoCopiar::FDestino Destino = FofuxoCopiar::DestinoDoEditor(*Editor);
	if (!Destino.Serve())
	{
		return;
	}

	// O cabecalho da secao diz para onde a pose vai. Sem isto o menu nao mostra em
	// qual dos dois lados se esta colando, e o botao de Fonte/Alvo fica longe.
	FToolMenuSection& Secao = Menu->FindOrAddSection("FofuxoCopiarPose", FText::Format(
		LOCTEXT("CopiarPara", "Colar em: {0} de {1}, pose \"{2}\""),
		FofuxoCopiar::NomeDoLado(Destino.Lado),
		FText::FromString(Destino.Asset->GetName()),
		FText::FromName(Destino.Pose)));

	TArray<FAssetData> Assets;
	FofuxoCopiar::Retargeters(Assets);

	const FSoftObjectPath CaminhoDoDestino(Destino.Asset);

	for (const FAssetData& Asset : Assets)
	{
		const FSoftObjectPath Caminho = Asset.GetSoftObjectPath();
		const bool bEEste = Caminho == CaminhoDoDestino;

		Secao.AddSubMenu(
			Asset.AssetName,
			bEEste
				? FText::Format(LOCTEXT("CopiarEste", "{0}  (este)"), FText::FromName(Asset.AssetName))
				: FText::FromName(Asset.AssetName),
			FText::FromString(Asset.PackageName.ToString()),
			FNewToolMenuChoice(FNewToolMenuDelegate::CreateLambda([Caminho](UToolMenu* Submenu)
			{
				// So agora o asset e carregado -- listar as poses exige abrir o
				// arquivo, e abrir todos os retargeters do projeto para desenhar um
				// menu seria caro por nada.
				MontarSubmenuDeUmRetargeter(Submenu, Caminho);
			})));
	}
}

void FFofuxoCopiarPose::MontarSubmenuDeUmRetargeter(UToolMenu* Menu, const FSoftObjectPath& Caminho)
{
	FIKRetargetEditor* Editor = EditorDoContexto(Menu->Context);
	if (Editor == nullptr)
	{
		return;
	}

	const FofuxoCopiar::FDestino Destino = FofuxoCopiar::DestinoDoEditor(*Editor);

	UIKRetargeter* Origem = Cast<UIKRetargeter>(Caminho.TryLoad());
	if (Origem == nullptr)
	{
		return;
	}

	UIKRetargeterController* DaOrigem = UIKRetargeterController::GetController(Origem);
	if (DaOrigem == nullptr)
	{
		return;
	}

	const bool bMesmoAsset = Origem == Destino.Asset;

	for (const ERetargetSourceOrTarget Lado : FofuxoCopiar::Lados)
	{
		const USkeletalMesh* Malha = DaOrigem->GetPreviewMesh(Lado);

		FToolMenuSection& Secao = Menu->FindOrAddSection(
			Lado == ERetargetSourceOrTarget::Source ? "Fonte" : "Alvo",
			Malha != nullptr
				? FText::Format(LOCTEXT("CopiarLadoComMalha", "{0} -- {1}"),
					FofuxoCopiar::NomeDoLado(Lado), FText::FromString(Malha->GetName()))
				: FofuxoCopiar::NomeDoLado(Lado));

		TArray<FName> Nomes;
		DaOrigem->GetRetargetPoses(Lado).GenerateKeyArray(Nomes);
		Nomes.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });

		for (const FName& Nome : Nomes)
		{
			// A propria pose que esta sendo editada nao e origem de nada.
			if (bMesmoAsset && Lado == Destino.Lado && Nome == Destino.Pose)
			{
				continue;
			}

			const FIKRetargetPose& Pose = DaOrigem->GetRetargetPoses(Lado)[Nome];
			const int32 Posados = FofuxoCopiar::QuantosPosados(Pose);

			Secao.AddMenuEntry(
				*FString::Printf(TEXT("%s_%s"),
					Lado == ERetargetSourceOrTarget::Source ? TEXT("Fonte") : TEXT("Alvo"), *Nome.ToString()),
				FText::FromName(Nome),
				FText::Format(
					LOCTEXT("CopiarPoseTip", "{0} ossos posados. A pose daqui e substituida por esta."),
					FText::AsNumber(Posados)),
				FSlateIcon(),
				FToolUIActionChoice(FToolUIAction(
					FToolMenuExecuteAction::CreateLambda(
						[Caminho, Lado, Nome](const FToolMenuContext& Contexto)
						{
							FFofuxoCopiarPose::Aplicar(Contexto, Caminho, Lado, Nome);
						}))));
		}
	}
}

void FFofuxoCopiarPose::Aplicar(
	const FToolMenuContext& Contexto,
	const FSoftObjectPath Caminho,
	const ERetargetSourceOrTarget LadoDaOrigem,
	const FName PoseDaOrigem)
{
	FIKRetargetEditor* Editor = EditorDoContexto(Contexto);
	if (Editor == nullptr)
	{
		return;
	}

	const FofuxoCopiar::FDestino Destino = FofuxoCopiar::DestinoDoEditor(*Editor);
	if (!Destino.Serve())
	{
		return;
	}

	UIKRetargeter* Origem = Cast<UIKRetargeter>(Caminho.TryLoad());
	if (Origem == nullptr)
	{
		return;
	}

	const FIKRetargetPose* Pose = Origem->GetRetargetPoseByName(LadoDaOrigem, PoseDaOrigem);
	if (Pose == nullptr)
	{
		return;
	}

	const FIKRetargetPose& Atual = Destino.AssetController->GetCurrentRetargetPose(Destino.Lado);

	const FofuxoCopiar::FConferencia Conta =
		FofuxoCopiar::Conferir(*Pose, Atual, Destino.Malha->GetRefSkeleton());

	if (Conta.Batem.IsEmpty() && Conta.Zeram.IsEmpty() && Conta.Pelvis.IsNearlyZero())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("CopiarNada",
				"A pose \"{0}\" de {1} nao tem nenhum osso posado que exista em {2}, e a pose daqui ja "
				"esta zerada. Nao ha o que copiar."),
			FText::FromName(PoseDaOrigem),
			FText::FromString(Origem->GetName()),
			FText::FromString(Destino.Malha->GetName())));

		return;
	}

	for (const FName& Osso : Conta.SemOsso)
	{
		UE_LOG(LogFofuxoCopiarPose, Display,
			TEXT("\"%s\" esta posado em %s mas nao existe em %s -- fora da copia."),
			*Osso.ToString(), *Origem->GetName(), *Destino.Malha->GetName());
	}

	const EAppReturnType::Type Resposta = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FofuxoCopiar::MontarPergunta(Conta, Destino, *Origem, LadoDaOrigem, PoseDaOrigem),
		LOCTEXT("TituloCopiar", "Copiar pose de retarget"));

	if (Resposta != EAppReturnType::Yes)
	{
		return;
	}

	const FScopedTransaction Transacao(LOCTEXT("CopiarTransacao", "Copiar pose de retarget"));

	Destino.Asset->Modify();

	// Uma reinicializacao so, no fim do escopo. Sem isto o retargeter se refaz uma
	// vez por osso, e uma pose inteira sao noventa e tantas.
	const FScopedReinitializeIKRetargeter Reinicializar(Destino.AssetController);

	// Zera antes de escrever: o que sobrasse aqui e nao viesse de la faria a pose
	// do destino ser a soma das duas, e nao a copia de uma.
	Destino.AssetController->ResetRetargetPose(Destino.Pose, TArray<FName>(), Destino.Lado);

	for (const TTuple<FName, FQuat>& Par : Conta.Batem)
	{
		Destino.AssetController->SetRotationOffsetForRetargetPoseBone(Par.Key, Par.Value, Destino.Lado);
	}

	if (!Conta.Pelvis.IsNearlyZero())
	{
		// O Reset acabou de deixar o deslocamento em zero, entao somar e o mesmo
		// que atribuir -- e somar e o unico verbo que o controlador oferece.
		Destino.AssetController->SetRootOffsetInRetargetPose(Conta.Pelvis, Destino.Lado);
	}

	UE_LOG(LogFofuxoCopiarPose, Display,
		TEXT("Copiei a pose \"%s\" do lado %s de %s para \"%s\" de %s: %d ossos, %d de fora, %d zerados."),
		*PoseDaOrigem.ToString(),
		*FofuxoCopiar::NomeDoLado(LadoDaOrigem).ToString(),
		*Origem->GetName(),
		*Destino.Pose.ToString(),
		*Destino.Asset->GetName(),
		Conta.Batem.Num(),
		Conta.SemOsso.Num(),
		Conta.Zeram.Num());
}

#undef LOCTEXT_NAMESPACE
