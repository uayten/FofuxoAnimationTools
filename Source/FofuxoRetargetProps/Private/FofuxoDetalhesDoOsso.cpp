// Fofuxo -- o painel Transforms do osso, editavel no Live Retarget

#include "FofuxoDetalhesDoOsso.h"

#include "FofuxoAjusteRodando.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "RetargetEditor/IKRetargetDetails.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "SAdvancedTransformInputBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

namespace FofuxoDetalhesDoOsso
{
	/**
	 * Se a escrita na pose de retarget vale agora.
	 *
	 * A engine responde `GetRetargeterMode() == EditRetargetPose` e para por ai.
	 * Aqui o Live Retarget entra pela mesma porta: no Running Retarget a escrita e
	 * a mesma, so que o resultado voce ve na animacao em vez de no ref pose.
	 *
	 * So no alvo, pela mesma razao que o gizmo: a animacao da fonte e o dado de
	 * entrada do retarget, e nao ha o que ajustar nela.
	 */
	static bool PodeEditar(TWeakPtr<FIKRetargetEditorController> Fraco)
	{
		const TSharedPtr<FIKRetargetEditorController> Quem = Fraco.Pin();
		if (!Quem.IsValid())
		{
			return false;
		}

		const ERetargeterOutputMode Modo = Quem->GetRetargeterMode();

		if (Modo == ERetargeterOutputMode::EditRetargetPose)
		{
			return true;
		}

		return FFofuxoAjusteRodando::EstaLigado()
			&& Modo == ERetargeterOutputMode::RunRetarget
			&& Quem->GetSourceOrTarget() == ERetargetSourceOrTarget::Target;
	}
}

void FFofuxoDetalhesDoOsso::Registrar()
{
	// A da engine tem que ja estar posta: registrar a mesma classe substitui a
	// anterior, e se a IKRigEditor subisse depois de nos ela desfaria isto. O
	// LoadModule de um modulo ja carregado nao faz nada.
	FModuleManager::Get().LoadModule(TEXT("IKRigEditor"));

	FPropertyEditorModule& Propriedades =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	Propriedades.RegisterCustomClassLayout(
		UIKRetargetBoneDetails::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FFofuxoDetalhesDoOsso::MakeInstance));
}

void FFofuxoDetalhesDoOsso::Esquecer()
{
	if (!FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		return;
	}

	FPropertyEditorModule& Propriedades =
		FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	// Devolver a da engine seria o ideal, e nao da: o MakeInstance dela e inline no
	// header, mas construir a classe pede a vtable, e os virtuais moram no .cpp da
	// IKRigEditor sem exportacao. Entao sai so o nosso registro. O painel do osso
	// fica sem customizacao nenhuma -- so o nome do osso, sem as linhas de
	// transform -- ate o proximo carregamento deste modulo. Isso e visivel num Live
	// Coding, e em nenhum outro momento.
	Propriedades.UnregisterCustomClassLayout(UIKRetargetBoneDetails::StaticClass()->GetFName());
}

TSharedRef<IDetailCustomization> FFofuxoDetalhesDoOsso::MakeInstance()
{
	return MakeShareable(new FFofuxoDetalhesDoOsso);
}

void FFofuxoDetalhesDoOsso::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(Ossos);
}

FString FFofuxoDetalhesDoOsso::GetReferencerName() const
{
	return TEXT("FFofuxoDetalhesDoOsso");
}

void FFofuxoDetalhesDoOsso::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	Ossos.Reset();

	for (const TWeakObjectPtr<UObject>& Objeto : DetailBuilder.GetSelectedObjects())
	{
		if (UIKRetargetBoneDetails* Osso = Cast<UIKRetargetBoneDetails>(Objeto.Get()))
		{
			Ossos.Add(Osso);
		}
	}

	if (Ossos.IsEmpty() || !Ossos[0]->EditorController.IsValid())
	{
		return;
	}

	const TWeakPtr<FIKRetargetEditorController> Fraco = Ossos[0]->EditorController;
	const TSharedPtr<FIKRetargetEditorController> Quem = Fraco.Pin();

	if (Quem->AssetController == nullptr)
	{
		return;
	}

	const ERetargetSourceOrTarget Lado = Quem->GetSourceOrTarget();
	const bool bPelvis = Ossos[0]->SelectedBone == Quem->AssetController->GetPelvisBone(Lado);

	// Isto so decide qual linha comeca aberta. Todas as quatro sao montadas de
	// qualquer jeito, e o botao troca entre elas sem reconstruir nada.
	const bool bPodeAgora = FofuxoDetalhesDoOsso::PodeEditar(Fraco);

	const TArray<EIKRetargetTransformType> Tipos =
	{
		EIKRetargetTransformType::RelativeOffset,
		EIKRetargetTransformType::Bone,
		EIKRetargetTransformType::Current,
		EIKRetargetTransformType::Reference,
	};

	const TArray<FText> Rotulos =
	{
		LOCTEXT("LinhaOffset", "Relative Offset"),
		LOCTEXT("LinhaBone", "Bone"),
		LOCTEXT("LinhaCurrent", "Current"),
		LOCTEXT("LinhaReference", "Reference"),
	};

	const TArray<FText> Dicas =
	{
		LOCTEXT("LinhaOffsetTip",
			"O quanto a pose de retarget afasta este osso do ref pose. E' o que o gizmo escreve, "
			"e o que o Alt+R zera."),
		LOCTEXT("LinhaBoneTip",
			"Onde a pose de retarget poe este osso, em relacao ao pai -- o ref pose ja somado ao "
			"offset acima."),
		LOCTEXT("LinhaCurrentTip",
			"Onde o osso esta agora no visor. Com uma animacao rodando, isto muda a cada quadro; "
			"e leitura, nunca escrita."),
		LOCTEXT("LinhaReferenceTip", "Onde o osso esta no ref pose da malha. Leitura."),
	};

	const TArray<EIKRetargetTransformType> Abertas =
	{
		bPodeAgora ? EIKRetargetTransformType::RelativeOffset : EIKRetargetTransformType::Current
	};

	const TSharedPtr<SSegmentedControl<EIKRetargetTransformType>> Escolha =
		SSegmentedControl<EIKRetargetTransformType>::Create(
			Tipos,
			Rotulos,
			Dicas,
			TAttribute<TArray<EIKRetargetTransformType>>(Abertas));

	DetailBuilder.EditCategory(TEXT("Selection")).SetSortOrder(1);

	IDetailCategoryBuilder& Categoria = DetailBuilder.EditCategory(TEXT("Transforms"));
	Categoria.SetSortOrder(2);

	Categoria.AddCustomRow(FText::FromString(TEXT("TransformType")))
	.ValueContent()
	.MinDesiredWidth(375.f)
	.MaxDesiredWidth(375.f)
	.HAlign(HAlign_Left)
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			Escolha.ToSharedRef()
		]
	];

	TArrayView<TObjectPtr<UIKRetargetBoneDetails>> Vista =
		TArrayView<TObjectPtr<UIKRetargetBoneDetails>>(Ossos);

	for (int32 Qual = 0; Qual < Tipos.Num(); ++Qual)
	{
		const EIKRetargetTransformType Tipo = Tipos[Qual];

		const bool bEscreve = Tipo == EIKRetargetTransformType::RelativeOffset
			|| Tipo == EIKRetargetTransformType::Bone;

		SAdvancedTransformInputBox<FTransform>::FArguments Args =
			SAdvancedTransformInputBox<FTransform>::FArguments()
			// Nas linhas de leitura o transform inteiro faz sentido. Nas de escrita
			// nao: quem grava location grava o deslocamento da raiz, que e um so para
			// a pose inteira, e escala a pose de retarget nao guarda.
			.ConstructLocation(!bEscreve || bPelvis)
			.ConstructRotation(true)
			.ConstructScale(!bEscreve)
			.DisplayRelativeWorld(true)
			.DisplayScaleLock(false)
			.AllowEditRotationRepresentation(true)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.UseQuaternionForRotation(true);

		if (bEscreve)
		{
			// Atributo, e nao valor: ligar o Live Retarget na barra nao reconstroi o
			// painel, entao um bool decidido aqui ficaria errado ate voce clicar
			// noutro osso.
			Args.IsEnabled(TAttribute<bool>::CreateLambda(
				[Fraco]() { return FofuxoDetalhesDoOsso::PodeEditar(Fraco); }));

			Args.OnNumericValueCommitted_Static(
				&UIKRetargetBoneDetails::OnMultiNumericValueCommitted,
				Tipo,
				Vista,
				true);

			Args.OnNumericValueChanged_Static(
				&UIKRetargetBoneDetails::OnMultiNumericValueCommitted,
				ETextCommit::Default,
				Tipo,
				Vista,
				false);

			Args.OnBeginSliderMovement_Lambda([](
				ESlateTransformComponent::Type,
				ESlateRotationRepresentation::Type,
				ESlateTransformSubComponent::Type)
			{
				GEditor->BeginTransaction(LOCTEXT("EditarPeloPainel", "Edit Retarget Pose Transform Slider"));
			});

			Args.OnEndSliderMovement_Lambda([](
				ESlateTransformComponent::Type,
				ESlateRotationRepresentation::Type,
				ESlateTransformSubComponent::Type,
				double)
			{
				GEditor->EndTransaction();
			});
		}
		else
		{
			Args.IsEnabled(false);
		}

		Args.OnGetIsComponentRelative_Lambda([Vista, Tipo](ESlateTransformComponent::Type Parte)
		{
			return Vista.ContainsByPredicate([&](const TObjectPtr<UIKRetargetBoneDetails> Osso)
			{
				return Osso->IsComponentRelative(Parte, Tipo);
			});
		});

		Args.OnIsComponentRelativeChanged_Lambda(
			[Vista, Tipo](ESlateTransformComponent::Type Parte, bool bRelativo)
		{
			for (const TObjectPtr<UIKRetargetBoneDetails>& Osso : Vista)
			{
				Osso->OnComponentRelativeChanged(Parte, bRelativo, Tipo);
			}
		});

		Args.OnGetNumericValue_Lambda([Vista, Tipo](
			ESlateTransformComponent::Type Parte,
			ESlateRotationRepresentation::Type Representacao,
			ESlateTransformSubComponent::Type Sub) -> TOptional<FVector::FReal>
		{
			if (Vista.IsEmpty() || !Vista[0]->IsValidLowLevel())
			{
				return TOptional<FVector::FReal>();
			}

			TOptional<FVector::FReal> Primeiro = Vista[0]->GetNumericValue(Tipo, Parte, Representacao, Sub);

			if (Primeiro)
			{
				for (int32 Outro = 1; Outro < Vista.Num(); ++Outro)
				{
					const TOptional<FVector::FReal> Valor =
						Vista[Outro]->GetNumericValue(Tipo, Parte, Representacao, Sub);

					if (Valor.IsSet())
					{
						// A mesma folga da engine: sem ela o ruido de virgula flutuante
						// das contas de rotacao faz o painel dizer "Multiple Values" com
						// dois ossos que estao no mesmo lugar.
						constexpr double Precisao = 1.e-2;
						if (!FMath::IsNearlyEqual(Primeiro.GetValue(), Valor.GetValue(), Precisao))
						{
							return TOptional<FVector::FReal>();
						}
					}
				}
			}

			return Primeiro;
		});

		Args.OnCopyToClipboard_UObject(Ossos[0].Get(), &UIKRetargetBoneDetails::OnCopyToClipboard, Tipo);
		Args.OnPasteFromClipboard_UObject(Ossos[0].Get(), &UIKRetargetBoneDetails::OnPasteFromClipboard, Tipo);

		Args.Visibility_Lambda([Escolha, Tipo]() -> EVisibility
		{
			return Escolha->HasValue(Tipo) ? EVisibility::Visible : EVisibility::Collapsed;
		});

		SAdvancedTransformInputBox<FTransform>::ConstructGroupedTransformRows(
			Categoria,
			Rotulos[Qual],
			Dicas[Qual],
			Args);
	}
}

#undef LOCTEXT_NAMESPACE
