// Fofuxo -- the retarget editor's keyboard shortcuts

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "FofuxoName.h"
#include "Styling/AppStyle.h"

/**
 * This plugin's commands inside the retarget editor.
 *
 * They are real FUICommandInfos, and not keys read by hand, for a practical
 * reason: a registered command shows up under Edit > Editor Preferences >
 * Keyboard Shortcuts, where the key can be changed. A key read by hand doesn't
 * show up there and there is no changing it.
 *
 * The context is our own ("FofuxoRetarget") and not the IK Retarget's: that one
 * belongs to IKRigEditor, and touching it from here would be writing into
 * another plugin's shortcuts.
 */
class FFofuxoCommands : public TCommands<FFofuxoCommands>
{
public:

	FFofuxoCommands()
		: TCommands<FFofuxoCommands>(
			TEXT("FofuxoRetarget"),
			FText::Format(NSLOCTEXT("Contexts", "FofuxoRetarget", "{0} -- Retarget"), Fofuxo::Name()),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{
	}

	/** Alt+R: gives the selected bones' rotation back to the ref pose. */
	TSharedPtr<FUICommandInfo> ResetRotation;

	virtual void RegisterCommands() override;
};
