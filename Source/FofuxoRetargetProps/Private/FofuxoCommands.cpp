// Fofuxo -- the retarget editor's keyboard shortcuts

#include "FofuxoCommands.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

void FFofuxoCommands::RegisterCommands()
{
	// Alt+R is Blender's, which is where the muscle memory comes from. Unreal
	// doesn't use that chord anywhere in the retarget editor, so there is nothing
	// to run over.
	UI_COMMAND(
		ResetRotation,
		"Reset the bone's rotation",
		"Gives back to the ref pose the rotation the retarget pose stores for the selected "
		"bones -- the same bones the toolbar's Reset Selected Bones would take.\n\n"
		"It works in both modes: in Editing Retarget Pose and, with Live Retarget on, also "
		"with the animation running. It does not touch the attachment op's offset.",
		EUserInterfaceActionType::Button,
		FInputChord(EModifierKey::Alt, EKeys::R));
}

#undef LOCTEXT_NAMESPACE
