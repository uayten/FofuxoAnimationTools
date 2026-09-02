// Fofuxo -- Alt+R on the selected bones

#include "FofuxoResetRotation.h"

#include "FofuxoCommands.h"

#include "Framework/Commands/UICommandList.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

void FFofuxoResetRotation::Register()
{
	FFofuxoCommands::Register();
}

void FFofuxoResetRotation::Forget()
{
	FFofuxoCommands::Unregister();
}

void FFofuxoResetRotation::EnsureShortcut(FIKRetargetEditor& Editor)
{
	const TSharedRef<FUICommandList> Commands = Editor.GetToolkitCommands();

	// This walk happens twice a second, and mapping the same command again would
	// stack one binding per pass.
	if (Commands->IsActionMapped(FFofuxoCommands::Get().ResetRotation))
	{
		return;
	}

	const TWeakPtr<FIKRetargetEditorController> Weak = Editor.GetController();

	Commands->MapAction(
		FFofuxoCommands::Get().ResetRotation,
		FExecuteAction::CreateStatic(&FFofuxoResetRotation::Reset, Weak),
		FCanExecuteAction::CreateStatic(&FFofuxoResetRotation::Can, Weak));
}

bool FFofuxoResetRotation::Can(TWeakPtr<FIKRetargetEditorController> Weak)
{
	const TSharedPtr<FIKRetargetEditorController> Who = Weak.Pin();

	return Who.IsValid()
		&& Who->AssetController != nullptr
		&& !Who->GetSelectedBones().IsEmpty();
}

void FFofuxoResetRotation::Reset(TWeakPtr<FIKRetargetEditorController> Weak)
{
	const TSharedPtr<FIKRetargetEditorController> Who = Weak.Pin();
	if (!Who.IsValid() || Who->AssetController == nullptr)
	{
		return;
	}

	// A copy, and not the reference: writing into the pose touches the retargeter,
	// and the selection list comes from inside it.
	const TArray<FName> Bones = Who->GetSelectedBones();
	if (Bones.IsEmpty())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ResetRotationTransaction", "Reset the bone's rotation"));

	if (UIKRetargeter* Asset = Who->AssetController->GetAsset())
	{
		Asset->Modify();
	}

	// One reinitialization at the end of the scope, and not one per bone: with a
	// whole hand selected that would be fifteen.
	const FScopedReinitializeIKRetargeter Reinitialize(Who->AssetController);

	const ERetargetSourceOrTarget Side = Who->GetSourceOrTarget();

	for (const FName& Bone : Bones)
	{
		Who->AssetController->SetRotationOffsetForRetargetPoseBone(Bone, FQuat::Identity, Side);
	}
}

#undef LOCTEXT_NAMESPACE
