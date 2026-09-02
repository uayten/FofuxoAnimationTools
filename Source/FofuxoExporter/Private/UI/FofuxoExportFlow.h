// Fofuxo -- from the selection to the file on disk

#pragma once

#include "CoreMinimal.h"

struct FAssetData;

/**
 * The whole export, from the Content Browser's selection to the notification at
 * the end: it gathers the animations and the mesh, loads the options, opens the
 * modal window, saves what the person ticked, calls the writer for the chosen
 * format and reports.
 *
 * It sits outside SFofuxoExportWindow because none of it is the widget's job --
 * the widget is a list with checkboxes, and it was only holding all of this
 * because it happened to be the file the menu entry called into.
 */
class FFofuxoExportFlow
{
public:

	static void Run(const TArray<FAssetData>& Selected);
};
