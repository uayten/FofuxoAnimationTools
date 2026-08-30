// Fofuxo -- salvar e aplicar a pose de retarget como asset

#include "FofuxoPoseNoDisco.h"

#include "FofuxoPoseDeRetarget.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "IContentBrowserSingleton.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
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
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoPoseNoDisco, Log, All);

namespace FofuxoDisco
{
	/** Quanto uma rotacao pode diferir da identidade e ainda contar como nao posada. */
	static constexpr float Folga = 1.0e-6f;

	/** O lado que o editor esta editando, com tudo que as duas pontas precisam. */
	struct FLado
	{
		UIKRetargeterController* AssetController = nullptr;
		UIKRetargeter* Asset = nullptr;
		USkeletalMesh* Malha = nullptr;
		ERetargetSourceOrTarget Qual = ERetargetSourceOrTarget::Source;
		FName Pose;

		bool Serve() const { return AssetController != nullptr && Asset != nullptr && Malha != nullptr; }
	};

	static FLado LadoDoEditor(FIKRetargetEditor& Editor)
	{
		const TSharedRef<FIKRetargetEditorController> Controlador = Editor.GetController();

		FLado Lado;
		Lado.AssetController = Controlador->AssetController;
		Lado.Qual = Controlador->GetSourceOrTarget();

		if (Lado.AssetController != nullptr)
		{
			Lado.Asset = Lado.AssetController->GetAsset();
			Lado.Malha = Lado.AssetController->GetPreviewMesh(Lado.Qual);
			Lado.Pose = Lado.AssetController->GetCurrentRetargetPoseName(Lado.Qual);
		}

		return Lado;
	}

	static FText NomeDoLado(const ERetargetSourceOrTarget Qual)
	{
		return Qual == ERetargetSourceOrTarget::Source
			? LOCTEXT("DiscoLadoFonte", "Fonte")
			: LOCTEXT("DiscoLadoAlvo", "Alvo");
	}

	/** O que a aplicacao vai fazer, contado antes de escrever qualquer coisa. */
	struct FConferencia
	{
		/** Osso e delta ja convertido para o ref pose *deste* esqueleto. */
		TArray<TTuple<FName, FQuat>> Batem;

		/** Ossos do asset que este esqueleto nao tem. */
		TArray<FName> SemOsso;

		/** Ossos do asset que aqui ja estao onde deveriam -- nao viram delta. */
		int32 JaBatem = 0;

		/** Ossos deste esqueleto que o asset nao menciona. */
		int32 SemNoticia = 0;

		/** Ossos posados aqui que a aplicacao devolve ao ref pose. */
		int32 Zeram = 0;

		FVector Pelvis = FVector::ZeroVector;
	};

	static FConferencia Conferir(
		const UFofuxoPoseDeRetarget& Guardada,
		const FIKRetargetPose& Atual,
		const FReferenceSkeleton& Esqueleto)
	{
		FConferencia Conta;
		Conta.Pelvis = Guardada.DeslocamentoDoPelvis;

		const TArray<FTransform>& Local = Esqueleto.GetRefBonePose();

		TSet<FName> Chegando;

		for (const TTuple<FName, FQuat>& Par : Guardada.RotacoesLocais)
		{
			const int32 Indice = Esqueleto.FindBoneIndex(Par.Key);
			if (Indice == INDEX_NONE)
			{
				Conta.SemOsso.Add(Par.Key);
				continue;
			}

			// De "onde o osso tem que ficar" para "o quanto ele sai do ref pose
			// daqui". Se os dois esqueletos forem o mesmo, isto devolve exatamente
			// o delta que estava la.
			const FQuat Delta =
				(Local[Indice].GetRotation().Inverse() * Par.Value).GetNormalized();

			if (Delta.Equals(FQuat::Identity, Folga))
			{
				++Conta.JaBatem;
				continue;
			}

			Conta.Batem.Emplace(Par.Key, Delta);
			Chegando.Add(Par.Key);
		}

		for (int32 Indice = 0; Indice < Esqueleto.GetNum(); ++Indice)
		{
			if (!Guardada.RotacoesLocais.Contains(Esqueleto.GetBoneName(Indice)))
			{
				++Conta.SemNoticia;
			}
		}

		for (const TTuple<FName, FQuat>& Par : Atual.GetAllDeltaRotations())
		{
			if (!Par.Value.Equals(FQuat::Identity, Folga) && !Chegando.Contains(Par.Key))
			{
				++Conta.Zeram;
			}
		}

		Conta.Batem.Sort([](const TTuple<FName, FQuat>& A, const TTuple<FName, FQuat>& B)
		{
			return A.Key.LexicalLess(B.Key);
		});

		Conta.SemOsso.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });

		return Conta;
	}

	static FText MontarPergunta(
		const FConferencia& Conta,
		const FLado& Lado,
		const UFofuxoPoseDeRetarget& Guardada)
	{
		FText Texto = FText::Format(
			LOCTEXT("DiscoPergunta",
				"Aplicar a pose de {0} na pose \"{1}\" do lado {2} de {3}?\n\n"
				"{4} ossos saem do ref pose de {5} para chegar onde a pose guardada os poe."),
			FText::FromString(Guardada.GetName()),
			FText::FromName(Lado.Pose),
			NomeDoLado(Lado.Qual),
			FText::FromString(Lado.Asset->GetName()),
			FText::AsNumber(Conta.Batem.Num()),
			FText::FromString(Lado.Malha->GetName()));

		if (Conta.JaBatem > 0)
		{
			Texto = FText::Format(
				LOCTEXT("DiscoJaBatem", "{0}\n{1} ja estao no lugar e nao precisam de nada."),
				Texto,
				FText::AsNumber(Conta.JaBatem));
		}

		if (Conta.SemOsso.Num() > 0)
		{
			Texto = FText::Format(
				LOCTEXT("DiscoSemOsso",
					"{0}\n{1} ossos do arquivo nao existem em {2} e ficam de fora (os nomes vao para o "
					"Output Log)."),
				Texto,
				FText::AsNumber(Conta.SemOsso.Num()),
				FText::FromString(Lado.Malha->GetName()));
		}

		if (Conta.SemNoticia > 0)
		{
			Texto = FText::Format(
				LOCTEXT("DiscoSemNoticia",
					"{0}\n{1} ossos daqui nao aparecem no arquivo e ficam no ref pose deste esqueleto."),
				Texto,
				FText::AsNumber(Conta.SemNoticia));
		}

		if (Conta.Zeram > 0)
		{
			Texto = FText::Format(
				LOCTEXT("DiscoZeram",
					"{0}\n{1} ossos que voce posou aqui voltam para o ref pose -- aplicar e ficar igual, "
					"nao somar."),
				Texto,
				FText::AsNumber(Conta.Zeram));
		}

		if (!Conta.Pelvis.IsNearlyZero())
		{
			Texto = FText::Format(
				LOCTEXT("DiscoPelvis",
					"{0}\n\nO deslocamento do pelvis vem junto ({1}). Esse e o unico valor em centimetros "
					"do arquivo: entre bonecos de tamanhos diferentes, confira depois."),
				Texto,
				FText::FromString(Conta.Pelvis.ToCompactString()));
		}

		if (!Guardada.Esqueleto.IsEmpty())
		{
			Texto = FText::Format(
				LOCTEXT("DiscoDeOndeVeio",
					"{0}\n\nO arquivo foi salvo de {1} ({2}).\nOs ossos sao casados por nome: dois "
					"esqueletos que sigam a convencao da Unreal batem, um que use outra convencao de eixo "
					"nao bate, e nao ha conversao que conserte isso."),
				Texto,
				FText::FromString(Guardada.Malha),
				FText::FromString(Guardada.Esqueleto));
		}

		return FText::Format(LOCTEXT("DiscoFecho", "{0}\n\nUm Ctrl+Z desfaz tudo de uma vez."), Texto);
	}
}

FIKRetargetEditor* FFofuxoPoseNoDisco::EditorDoContexto(const FToolMenuContext& Contexto)
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

void FFofuxoPoseNoDisco::MontarSecao(UToolMenu* Menu)
{
	FToolMenuSection& Secao = Menu->FindOrAddSection(
		"FofuxoPoseNoDisco", LOCTEXT("DiscoSecao", "Do disco -- atravessa projeto"));

	Secao.AddMenuEntry(
		"FofuxoSalvarPose",
		LOCTEXT("DiscoSalvar", "Salvar esta pose num asset..."),
		LOCTEXT("DiscoSalvarTip",
			"Grava a pose do lado que voce esta editando num asset de pose, que e um arquivo como outro "
			"qualquer -- da para copiar para outro projeto pelo explorador, ou pelo Migrate.\n\n"
			"O que vai gravado e a rotacao final de cada osso, e nao o delta. E por isso que a pose do "
			"Manny daqui serve num MetaHuman: o delta e a correcao medida do ref pose de quem a fez, e "
			"a rotacao final e a pose."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Save"),
		FToolUIActionChoice(FToolUIAction(
			FToolMenuExecuteAction::CreateStatic(&FFofuxoPoseNoDisco::Salvar))));

	Secao.AddMenuEntry(
		"FofuxoAplicarPose",
		LOCTEXT("DiscoAplicar", "Aplicar a pose de um asset..."),
		LOCTEXT("DiscoAplicarTip",
			"Substitui a pose do lado que voce esta editando pela de um asset de pose, casando os ossos "
			"pelo nome.\n\n"
			"A pergunta antes de escrever diz quantos ossos se mexem, quantos ja estao no lugar, quantos "
			"ficam de fora e quantos voltam para o ref pose."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.OpenFile"),
		FToolUIActionChoice(FToolUIAction(
			FToolMenuExecuteAction::CreateStatic(&FFofuxoPoseNoDisco::Aplicar))));
}

void FFofuxoPoseNoDisco::Salvar(const FToolMenuContext& Contexto)
{
	FIKRetargetEditor* Editor = EditorDoContexto(Contexto);
	if (Editor == nullptr)
	{
		return;
	}

	const FofuxoDisco::FLado Lado = FofuxoDisco::LadoDoEditor(*Editor);
	if (!Lado.Serve())
	{
		return;
	}

	const FIKRetargetPose& Pose = Lado.AssetController->GetCurrentRetargetPose(Lado.Qual);
	const TMap<FName, FQuat>& Deltas = Pose.GetAllDeltaRotations();

	FString NomeSugerido = FString::Printf(TEXT("FPOSE_%s_%s"),
		*Lado.Malha->GetName(), *Lado.Pose.ToString());
	NomeSugerido.RemoveSpacesInline();

	FSaveAssetDialogConfig Config;
	Config.DefaultPath = FPaths::GetPath(Lado.Asset->GetPackage()->GetPathName());
	Config.DefaultAssetName = NomeSugerido;
	Config.AssetClassNames.Add(UFofuxoPoseDeRetarget::StaticClass()->GetClassPathName());
	Config.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::AllowButWarn;
	Config.DialogTitleOverride = LOCTEXT("DiscoSalvarTitulo", "Salvar a pose de retarget");

	const FContentBrowserModule& Navegador =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	const FString Escolhido = Navegador.Get().CreateModalSaveAssetDialog(Config);
	if (Escolhido.IsEmpty())
	{
		return;
	}

	const FString CaminhoDoPacote = FPackageName::ObjectPathToPackageName(Escolhido);
	const FString NomeDoAsset = FPaths::GetBaseFilename(CaminhoDoPacote, /*bRemovePath*/ true);

	UPackage* Pacote = CreatePackage(*CaminhoDoPacote);
	if (Pacote == nullptr)
	{
		return;
	}

	UFofuxoPoseDeRetarget* Guardada = NewObject<UFofuxoPoseDeRetarget>(
		Pacote, UFofuxoPoseDeRetarget::StaticClass(), FName(*NomeDoAsset), RF_Public | RF_Standalone);

	const FReferenceSkeleton& Esqueleto = Lado.Malha->GetRefSkeleton();
	const TArray<FTransform>& Local = Esqueleto.GetRefBonePose();

	int32 Posados = 0;

	for (int32 Indice = 0; Indice < Esqueleto.GetNum(); ++Indice)
	{
		const FName Osso = Esqueleto.GetBoneName(Indice);

		const FQuat* Achado = Deltas.Find(Osso);
		const FQuat Delta = Achado != nullptr ? *Achado : FQuat::Identity;

		if (!Delta.Equals(FQuat::Identity, FofuxoDisco::Folga))
		{
			++Posados;
		}

		Guardada->RotacoesLocais.Add(
			Osso, (Local[Indice].GetRotation() * Delta).GetNormalized());
	}

	Guardada->DeslocamentoDoPelvis = Pose.GetRootTranslationDelta();
	Guardada->OssosPosados = Posados;
	Guardada->Malha = Lado.Malha->GetName();
	Guardada->Esqueleto = Lado.Malha->GetSkeleton() != nullptr ? Lado.Malha->GetSkeleton()->GetName() : FString();
	Guardada->Retargeter = Lado.Asset->GetName();
	Guardada->Lado = FofuxoDisco::NomeDoLado(Lado.Qual).ToString();
	Guardada->NomeDaPose = Lado.Pose.ToString();
	Guardada->Quando = FDateTime::Now().ToString(TEXT("%d/%m/%Y"));

	FAssetRegistryModule::AssetCreated(Guardada);
	Pacote->MarkPackageDirty();

	UE_LOG(LogFofuxoPoseNoDisco, Display,
		TEXT("Salvei a pose \"%s\" de %s em %s: %d ossos, %d posados."),
		*Lado.Pose.ToString(), *Lado.Malha->GetName(), *CaminhoDoPacote,
		Guardada->RotacoesLocais.Num(), Posados);

	FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
		LOCTEXT("DiscoSalvou",
			"Salvei {0} com a pose inteira de {1}: {2} ossos, dos quais {3} estao posados.\n\n"
			"O asset ainda nao esta no disco -- ele nasce sujo, como qualquer asset novo. Salve o "
			"projeto para o arquivo existir."),
		FText::FromString(NomeDoAsset),
		FText::FromString(Lado.Malha->GetName()),
		FText::AsNumber(Guardada->RotacoesLocais.Num()),
		FText::AsNumber(Posados)));
}

void FFofuxoPoseNoDisco::Aplicar(const FToolMenuContext& Contexto)
{
	FIKRetargetEditor* Editor = EditorDoContexto(Contexto);
	if (Editor == nullptr)
	{
		return;
	}

	const FofuxoDisco::FLado Lado = FofuxoDisco::LadoDoEditor(*Editor);
	if (!Lado.Serve())
	{
		return;
	}

	FOpenAssetDialogConfig Config;
	Config.AssetClassNames.Add(UFofuxoPoseDeRetarget::StaticClass()->GetClassPathName());
	Config.bAllowMultipleSelection = false;
	Config.DialogTitleOverride = LOCTEXT("DiscoAplicarTitulo", "Aplicar uma pose de retarget");

	const FContentBrowserModule& Navegador =
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	const TArray<FAssetData> Escolhidos = Navegador.Get().CreateModalOpenAssetDialog(Config);
	if (Escolhidos.IsEmpty())
	{
		return;
	}

	UFofuxoPoseDeRetarget* Guardada = Cast<UFofuxoPoseDeRetarget>(Escolhidos[0].GetAsset());
	if (Guardada == nullptr)
	{
		return;
	}

	const FIKRetargetPose& Atual = Lado.AssetController->GetCurrentRetargetPose(Lado.Qual);

	const FofuxoDisco::FConferencia Conta =
		FofuxoDisco::Conferir(*Guardada, Atual, Lado.Malha->GetRefSkeleton());

	if (Conta.Batem.IsEmpty() && Conta.Zeram == 0 && Conta.Pelvis.IsNearlyZero())
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("DiscoNada",
				"{0} nao muda nada em {1}: os ossos que batem ja estao onde a pose guardada os poe, e a "
				"pose daqui ja esta zerada."),
			FText::FromString(Guardada->GetName()),
			FText::FromString(Lado.Malha->GetName())));

		return;
	}

	for (const FName& Osso : Conta.SemOsso)
	{
		UE_LOG(LogFofuxoPoseNoDisco, Display,
			TEXT("\"%s\" esta em %s mas nao existe em %s -- fora da aplicacao."),
			*Osso.ToString(), *Guardada->GetName(), *Lado.Malha->GetName());
	}

	const EAppReturnType::Type Resposta = FMessageDialog::Open(
		EAppMsgType::YesNo,
		FofuxoDisco::MontarPergunta(Conta, Lado, *Guardada),
		LOCTEXT("DiscoTitulo", "Aplicar pose de retarget"));

	if (Resposta != EAppReturnType::Yes)
	{
		return;
	}

	const FScopedTransaction Transacao(LOCTEXT("DiscoTransacao", "Aplicar pose de retarget"));

	Lado.Asset->Modify();

	// Uma reinicializacao so, no fim do escopo -- e nao uma por osso.
	const FScopedReinitializeIKRetargeter Reinicializar(Lado.AssetController);

	// Zera antes de escrever: o que sobrasse aqui e nao viesse do arquivo faria a
	// pose ser a soma das duas, e nao a copia de uma.
	Lado.AssetController->ResetRetargetPose(Lado.Pose, TArray<FName>(), Lado.Qual);

	for (const TTuple<FName, FQuat>& Par : Conta.Batem)
	{
		Lado.AssetController->SetRotationOffsetForRetargetPoseBone(Par.Key, Par.Value, Lado.Qual);
	}

	if (!Conta.Pelvis.IsNearlyZero())
	{
		// O Reset acabou de deixar o deslocamento em zero, entao somar e o mesmo
		// que atribuir -- e somar e o unico verbo que o controlador oferece.
		Lado.AssetController->SetRootOffsetInRetargetPose(Conta.Pelvis, Lado.Qual);
	}

	UE_LOG(LogFofuxoPoseNoDisco, Display,
		TEXT("Apliquei %s em \"%s\" do lado %s de %s: %d ossos escritos, %d ja no lugar, %d de fora."),
		*Guardada->GetName(),
		*Lado.Pose.ToString(),
		*FofuxoDisco::NomeDoLado(Lado.Qual).ToString(),
		*Lado.Asset->GetName(),
		Conta.Batem.Num(),
		Conta.JaBatem,
		Conta.SemOsso.Num());
}

#undef LOCTEXT_NAMESPACE
