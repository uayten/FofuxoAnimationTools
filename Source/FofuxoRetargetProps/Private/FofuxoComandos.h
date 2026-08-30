// Fofuxo -- os atalhos de teclado do editor de retarget

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "FofuxoNome.h"
#include "Styling/AppStyle.h"

/**
 * Os comandos deste plugin dentro do editor de retarget.
 *
 * Sao FUICommandInfo de verdade, e nao teclas lidas na mao, por um motivo
 * pratico: comando registrado aparece em Editar > Preferencias do Editor >
 * Atalhos de Teclado, onde da para trocar a tecla. Tecla lida na mao nao aparece
 * la e nao ha como trocar.
 *
 * O contexto e proprio ("FofuxoRetarget") e nao o do IK Retarget: o do editor e
 * da IKRigEditor, e mexer nele daqui seria escrever no atalho de outro plugin.
 */
class FFofuxoComandos : public TCommands<FFofuxoComandos>
{
public:
	FFofuxoComandos()
		: TCommands<FFofuxoComandos>(
			TEXT("FofuxoRetarget"),
			FText::Format(NSLOCTEXT("Contexts", "FofuxoRetarget", "{0} -- Retarget"), Fofuxo::Nome()),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{
	}

	/** Alt+R: devolve ao ref pose a rotacao dos ossos selecionados. */
	TSharedPtr<FUICommandInfo> ZerarRotacao;

	virtual void RegisterCommands() override;
};
