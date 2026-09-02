// Fofuxo

#include "FofuxoSceneWriters.h"

FFofuxoWriteScene& FofuxoUsdSceneWriter()
{
	// A function-local static, not a file-scope global: the module that binds
	// this one loads after this one, and a global might not be constructed in
	// time.
	static FFofuxoWriteScene Writer;
	return Writer;
}

FFofuxoWriteScene& FofuxoGltfSceneWriter()
{
	static FFofuxoWriteScene Writer;
	return Writer;
}
