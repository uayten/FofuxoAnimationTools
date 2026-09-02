// Fofuxo

#pragma once

#include "CoreMinimal.h"

struct FFofuxoExportRequest;

/**
 * Writes an FBX with the mesh and every animation, each one a take named after
 * the asset.
 *
 * The engine's exporter only knows how to write one take per document, and its
 * FbxAnimStack is a private member. What we do here is let it write the first
 * take normally, grab the scene from the node it hands back, and append the
 * other takes by hand, each in its own stack and layer.
 */
class FFofuxoFbxWriter
{
public:

	static bool Export(const FFofuxoExportRequest& Request, FText& OutError);
};
