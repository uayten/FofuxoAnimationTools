// Fofuxo -- the plugin's name, in one place

#pragma once

#include "Internationalization/Text.h"

/**
 * The plugin's name.
 *
 * Renaming a plugin is usually a hunt across the whole codebase. Not here: the
 * two lines below are the source of everything the user reads.
 *
 * **What these two lines cover:** every piece of UI text -- window title, menu
 * entry, progress bar, the name of the shortcut context.
 *
 * **What they don't cover, and can't:**
 *
 * - The `FriendlyName` in the `.uplugin`, which is JSON and is read by the
 *   engine before any C++ exists. That is the other place to change, and there
 *   are only the two.
 * - The plugin's folder name and the module names (`FofuxoExporter`,
 *   `FofuxoRetargetProps`, ...). Those are plumbing: they show up in file paths,
 *   in `IMPLEMENT_MODULE`, in the `.uplugin` and in the repository path.
 *   Changing them is work that changes nothing you can see.
 * - The file headers, which say only `// Fofuxo -- subject`. That is deliberate:
 *   a comment repeating the full name would be one more place to forget.
 *
 * This header lives in `Source/FofuxoCommon/`, which is not a module -- it is an
 * include folder that each interested Build.cs puts in `PublicIncludePaths`.
 * That way no module ends up depending on another one just for a string.
 */
#define FOFUXO_NAME TEXT("Fofuxo's Animation Tools")

/** The same name where the full one doesn't fit: menu entry, button, toolbar. */
#define FOFUXO_SHORT_NAME TEXT("Fofuxo")

namespace Fofuxo
{
	/** The full name, ready for the UI. */
	inline FText Name()
	{
		return FText::FromString(FOFUXO_NAME);
	}

	/** The short name, ready for the UI. */
	inline FText ShortName()
	{
		return FText::FromString(FOFUXO_SHORT_NAME);
	}
}
