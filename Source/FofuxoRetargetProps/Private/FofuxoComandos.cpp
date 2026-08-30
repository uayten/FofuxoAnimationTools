// Fofuxo -- os atalhos de teclado do editor de retarget

#include "FofuxoComandos.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

void FFofuxoComandos::RegisterCommands()
{
	// Alt+R e o do Blender, que e de onde vem a mao. A Unreal nao usa esse acorde
	// em lugar nenhum do editor de retarget, entao nao ha o que atropelar.
	UI_COMMAND(
		ZerarRotacao,
		"Zerar rotacao do osso",
		"Devolve ao ref pose a rotacao que a pose de retarget guarda para os ossos "
		"selecionados -- os mesmos ossos que o Reset Selected Bones da barra levaria.\n\n"
		"Vale nos dois modos: no Editing Retarget Pose e, com o Live Retarget ligado, "
		"tambem com a animacao rodando. Nao mexe no deslocamento do op de anexos.",
		EUserInterfaceActionType::Button,
		FInputChord(EModifierKey::Alt, EKeys::R));
}

#undef LOCTEXT_NAMESPACE
