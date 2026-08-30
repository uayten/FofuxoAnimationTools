// Fofuxo -- o botao Alinhar dentro do op dos anexos

#include "FofuxoAnexoDetalhes.h"

#include "FofuxoAnexosOp.h"
#include "FofuxoEsticarOssos.h"

#include "DetailWidgetRow.h"
#include "Editor.h"
#include "IDetailChildrenBuilder.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"
#include "ScopedTransaction.h"
#include "StructUtils/InstancedStruct.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoAnexoUI, Log, All);

namespace FofuxoAnexoUI
{
	/** A linha por tras do handle, ou nullptr se o painel estiver mostrando varias. */
	static FFofuxoAnexo* LinhaDoHandle(const TSharedRef<IPropertyHandle>& Handle)
	{
		TArray<void*> Enderecos;
		Handle->AccessRawData(Enderecos);

		// Mais de um endereco e edicao em lote, e ai nao ha "o osso desta linha".
		return Enderecos.Num() == 1 ? static_cast<FFofuxoAnexo*>(Enderecos[0]) : nullptr;
	}

	/**
	 * O retargeter dono desta linha, achado pelo endereco dela.
	 *
	 * A lista mora dentro do asset, entao o retargeter aberto cujo op contem *este*
	 * FFofuxoAnexo e o dono, sem ambiguidade possivel. O caminho pelo painel de
	 * detalhes existe, mas passa por wrappers de UObject sem API exportada.
	 */
	static UIKRetargeter* DonoDaLinha(const FFofuxoAnexo* Linha)
	{
		if (Linha == nullptr || GEditor == nullptr)
		{
			return nullptr;
		}

		UAssetEditorSubsystem* Subsistema = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (Subsistema == nullptr)
		{
			return nullptr;
		}

		for (UObject* Editado : Subsistema->GetAllEditedAssets())
		{
			UIKRetargeter* Retargeter = Cast<UIKRetargeter>(Editado);
			if (Retargeter == nullptr)
			{
				continue;
			}

			for (const FInstancedStruct& Op : Retargeter->GetRetargetOps())
			{
				const FFofuxoAnexosOp* Nosso = Op.GetPtr<FFofuxoAnexosOp>();
				if (Nosso == nullptr)
				{
					continue;
				}

				for (const FFofuxoAnexo& Cada : Nosso->Settings.Anexos)
				{
					if (&Cada == Linha)
					{
						return Retargeter;
					}
				}
			}
		}

		return nullptr;
	}

	/** Um osso a alinhar, de um lado. */
	struct FPasso
	{
		ERetargetSourceOrTarget Lado = ERetargetSourceOrTarget::Source;
		FName Osso;
		FQuat Delta = FQuat::Identity;
	};

	static FText NomeDoLado(const ERetargetSourceOrTarget Lado)
	{
		return Lado == ERetargetSourceOrTarget::Source
			? LOCTEXT("AnexoLadoFonte", "fonte")
			: LOCTEXT("AnexoLadoAlvo", "alvo");
	}

	static FReply Alinhar(TSharedRef<IPropertyHandle> Handle)
	{
		FFofuxoAnexo* Linha = LinhaDoHandle(Handle);
		UIKRetargeter* Asset = DonoDaLinha(Linha);

		if (Linha == nullptr || Asset == nullptr)
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT("AnexoSemDono",
					"Nao consegui achar o retargeter desta linha. Isto acontece quando o painel esta "
					"mostrando mais de um anexo de uma vez -- selecione um so."));

			return FReply::Handled();
		}

		UIKRetargeterController* Controlador = UIKRetargeterController::GetController(Asset);
		if (Controlador == nullptr)
		{
			return FReply::Handled();
		}

		TArray<FPasso> Passos;

		if (Linha->Boneco != EFofuxoBoneco::Alvo)
		{
			Passos.Add({ ERetargetSourceOrTarget::Source, Linha->OssoNaFonte.BoneName });
		}

		if (Linha->Boneco != EFofuxoBoneco::Fonte)
		{
			Passos.Add({ ERetargetSourceOrTarget::Target, Linha->OssoNoAlvo.BoneName });
		}

		TArray<FPasso> AEscrever;

		for (FPasso& Passo : Passos)
		{
			if (Passo.Osso.IsNone())
			{
				continue;
			}

			if (FFofuxoEsticarOssos::DeltaParaOMundo(
				*Controlador, Passo.Lado, Passo.Osso, Linha->Eixo, Passo.Delta))
			{
				AEscrever.Add(Passo);
			}
			else
			{
				UE_LOG(LogFofuxoAnexoUI, Warning,
					TEXT("\"%s\" nao existe no esqueleto do lado %s -- fora do alinhamento."),
					*Passo.Osso.ToString(), *NomeDoLado(Passo.Lado).ToString());
			}
		}

		if (AEscrever.IsEmpty())
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT("AnexoSemOsso",
					"Esta linha nao tem osso para alinhar: os campos estao vazios, ou o nome escolhido nao "
					"existe no esqueleto daquele lado. O Output Log diz qual foi o caso."));

			return FReply::Handled();
		}

		const FScopedTransaction Transacao(LOCTEXT("AnexoAlinharTransacao", "Alinhar no mundo"));

		Asset->Modify();

		// Uma reinicializacao so, no fim do escopo, mesmo alinhando os dois lados.
		const FScopedReinitializeIKRetargeter Reinicializar(Controlador);

		for (const FPasso& Passo : AEscrever)
		{
			Controlador->SetRotationOffsetForRetargetPoseBone(Passo.Osso, Passo.Delta, Passo.Lado);

			UE_LOG(LogFofuxoAnexoUI, Display,
				TEXT("Alinhei \"%s\" (%s) nos eixos do mundo."),
				*Passo.Osso.ToString(), *NomeDoLado(Passo.Lado).ToString());
		}

		return FReply::Handled();
	}

	/**
	 * O que a linha e, em uma linha -- para o cabecalho, com a linha fechada.
	 *
	 * "Index [0]" e "6 members" nao dizem qual das duas armas e qual, que e
	 * justamente o que se precisa saber para apagar a certa.
	 */
	static FText ResumoDaLinha(TSharedRef<IPropertyHandle> Handle)
	{
		const FFofuxoAnexo* Linha = LinhaDoHandle(Handle);
		if (Linha == nullptr)
		{
			return FText::GetEmpty();
		}

		const FString Asset = Linha->Asset.IsNull()
			? LOCTEXT("AnexoSemAsset", "sem asset").ToString()
			: Linha->Asset.GetAssetName();

		// Com o anexo nos dois bonecos, o osso mostrado e o do alvo: e o esqueleto
		// que esta sendo consertado, e o nome que muda de personagem para personagem.
		const FName Osso = Linha->Boneco == EFofuxoBoneco::Fonte
			? Linha->OssoNaFonte.BoneName
			: Linha->OssoNoAlvo.BoneName;

		if (Osso.IsNone())
		{
			return FText::FromString(Asset);
		}

		return FText::Format(
			LOCTEXT("AnexoResumo", "{0}  em  {1}"),
			FText::FromString(Asset),
			FText::FromName(Osso));
	}

	/** O texto do botao muda com o que ele vai fazer: um lado, ou os dois. */
	static FText RotuloDoBotao(TSharedRef<IPropertyHandle> Handle)
	{
		const FFofuxoAnexo* Linha = LinhaDoHandle(Handle);

		if (Linha != nullptr && Linha->Boneco == EFofuxoBoneco::Ambos)
		{
			return LOCTEXT("AnexoAlinharDois", "Alinhar os dois no mundo");
		}

		return LOCTEXT("AnexoAlinharUm", "Alinhar no mundo");
	}
}

TSharedRef<IPropertyTypeCustomization> FFofuxoAnexoDetalhes::Criar()
{
	return MakeShared<FFofuxoAnexoDetalhes>();
}

void FFofuxoAnexoDetalhes::Registrar()
{
	FPropertyEditorModule& Painel =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	Painel.RegisterCustomPropertyTypeLayout(
		FFofuxoAnexo::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FFofuxoAnexoDetalhes::Criar));
}

void FFofuxoAnexoDetalhes::Esquecer()
{
	// Sem LoadModuleChecked: no desligamento do editor o PropertyEditor pode ja ter
	// saido, e carrega-lo de novo so para desregistrar seria pior.
	if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
	{
		FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"))
			.UnregisterCustomPropertyTypeLayout(FFofuxoAnexo::StaticStruct()->GetFName());
	}
}

void FFofuxoAnexoDetalhes::CustomizeHeader(
	TSharedRef<IPropertyHandle> Handle,
	FDetailWidgetRow& Linha,
	IPropertyTypeCustomizationUtils& Utilidades)
{
	// A caixinha do Mostrar sobe para o cabecalho, e por isso ela nao entra na lista
	// de filhos la embaixo. E' o unico campo que se usa com a linha fechada:
	// esconder um anexo para ajustar o outro nao devia obrigar a abrir os dois.
	const TSharedPtr<IPropertyHandle> Mostrar =
		Handle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FFofuxoAnexo, bMostrar));

	Linha
		.NameContent()
		[
			Handle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(260.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				Mostrar.IsValid() ? Mostrar->CreatePropertyValueWidget() : SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([Handle]() { return FofuxoAnexoUI::ResumoDaLinha(Handle); })
				.Font(IPropertyTypeCustomizationUtils::GetRegularFont())
			]
		];
}

void FFofuxoAnexoDetalhes::CustomizeChildren(
	TSharedRef<IPropertyHandle> Handle,
	IDetailChildrenBuilder& Construtor,
	IPropertyTypeCustomizationUtils& Utilidades)
{
	uint32 Quantos = 0;
	Handle->GetNumChildren(Quantos);

	static const FName NoCabecalho = GET_MEMBER_NAME_CHECKED(FFofuxoAnexo, bMostrar);

	// Um por um, na ordem em que estao declarados. O AddProperty respeita o
	// EditCondition, entao os campos de osso continuam aparecendo e sumindo com o
	// Boneco escolhido.
	for (uint32 Indice = 0; Indice < Quantos; ++Indice)
	{
		const TSharedPtr<IPropertyHandle> Filho = Handle->GetChildHandle(Indice);
		if (!Filho.IsValid())
		{
			continue;
		}

		// O Mostrar ja esta no cabecalho; repetir aqui seriam duas caixinhas para a
		// mesma coisa.
		if (Filho->GetProperty() != nullptr && Filho->GetProperty()->GetFName() == NoCabecalho)
		{
			continue;
		}

		Construtor.AddProperty(Filho.ToSharedRef());
	}

	Construtor.AddCustomRow(LOCTEXT("AnexoAlinharBusca", "Alinhar no mundo"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AnexoAlinharNome", "Alinhar"))
			.Font(Utilidades.GetRegularFont())
		]
		.ValueContent()
		.MinDesiredWidth(200.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text_Lambda([Handle]() { return FofuxoAnexoUI::RotuloDoBotao(Handle); })
			.ToolTipText(LOCTEXT("AnexoAlinharTip",
				"Poe o osso desta linha com os eixos em cima dos eixos do mundo, na pose de retarget do "
				"lado dele -- o mesmo que o Alinhar no mundo da barra, so que no osso que a linha ja "
				"nomeia, sem precisar seleciona-lo no visor.\n\n"
				"Num anexo em Ambos ele alinha os dois lados de uma vez, cada um no seu osso, num "
				"Ctrl+Z so. E' o ponto: os dois ficam na mesma orientacao por causa de uma referencia "
				"externa aos dois, e nao de uma medida tirada de um deles.\n\n"
				"Funciona fora do Editing Retarget Pose, mas so naquele modo da para ver o resultado."))
			.OnClicked_Lambda([Handle]() { return FofuxoAnexoUI::Alinhar(Handle); })
		];
}

#undef LOCTEXT_NAMESPACE
