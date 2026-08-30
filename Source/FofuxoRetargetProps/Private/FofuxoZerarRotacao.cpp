// Fofuxo -- Alt+R nos ossos selecionados

#include "FofuxoZerarRotacao.h"

#include "FofuxoComandos.h"

#include "Framework/Commands/UICommandList.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

void FFofuxoZerarRotacao::Registrar()
{
	FFofuxoComandos::Register();
}

void FFofuxoZerarRotacao::Esquecer()
{
	FFofuxoComandos::Unregister();
}

void FFofuxoZerarRotacao::GarantirAtalho(FIKRetargetEditor& Editor)
{
	const TSharedRef<FUICommandList> Comandos = Editor.GetToolkitCommands();

	// Este passeio acontece duas vezes por segundo, e mapear de novo o mesmo
	// comando empilharia uma ligacao por passada.
	if (Comandos->IsActionMapped(FFofuxoComandos::Get().ZerarRotacao))
	{
		return;
	}

	const TWeakPtr<FIKRetargetEditorController> Fraco = Editor.GetController();

	Comandos->MapAction(
		FFofuxoComandos::Get().ZerarRotacao,
		FExecuteAction::CreateStatic(&FFofuxoZerarRotacao::Zerar, Fraco),
		FCanExecuteAction::CreateStatic(&FFofuxoZerarRotacao::Pode, Fraco));
}

bool FFofuxoZerarRotacao::Pode(TWeakPtr<FIKRetargetEditorController> Fraco)
{
	const TSharedPtr<FIKRetargetEditorController> Quem = Fraco.Pin();

	return Quem.IsValid()
		&& Quem->AssetController != nullptr
		&& !Quem->GetSelectedBones().IsEmpty();
}

void FFofuxoZerarRotacao::Zerar(TWeakPtr<FIKRetargetEditorController> Fraco)
{
	const TSharedPtr<FIKRetargetEditorController> Quem = Fraco.Pin();
	if (!Quem.IsValid() || Quem->AssetController == nullptr)
	{
		return;
	}

	// Uma copia, e nao a referencia: escrever na pose mexe no retargeter, e a
	// lista de selecao vem de dentro dele.
	const TArray<FName> Ossos = Quem->GetSelectedBones();
	if (Ossos.IsEmpty())
	{
		return;
	}

	const FScopedTransaction Transacao(LOCTEXT("ZerarRotacaoTransacao", "Zerar rotacao do osso"));

	if (UIKRetargeter* Asset = Quem->AssetController->GetAsset())
	{
		Asset->Modify();
	}

	// Uma reinicializacao no fim do escopo, e nao uma por osso: com a mao inteira
	// selecionada seriam quinze.
	const FScopedReinitializeIKRetargeter Reinicializar(Quem->AssetController);

	const ERetargetSourceOrTarget Lado = Quem->GetSourceOrTarget();

	for (const FName& Osso : Ossos)
	{
		Quem->AssetController->SetRotationOffsetForRetargetPoseBone(Osso, FQuat::Identity, Lado);
	}
}

#undef LOCTEXT_NAMESPACE
