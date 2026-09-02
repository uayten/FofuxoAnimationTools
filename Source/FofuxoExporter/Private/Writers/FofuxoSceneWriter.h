// Fofuxo

#pragma once

#include "CoreMinimal.h"

struct FFofuxoExportRequest;

/**
 * The formats that write a scene: USD and glTF.
 *
 * Both hold a single skeleton with several animations hanging off it, and both
 * are written by separate modules -- this one slices into batches, builds the
 * request and calls whoever is bound. Not one line of USD or glTF passes through
 * the main module, and that is what lets the plugin load in a project that
 * doesn't have those engine plugins.
 */
class FFofuxoSceneWriter
{
public:

	static bool Export(const FFofuxoExportRequest& Request, FText& OutError);
};
