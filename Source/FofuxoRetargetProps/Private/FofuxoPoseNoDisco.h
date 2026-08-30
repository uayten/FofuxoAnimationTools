// Fofuxo -- salvar e aplicar a pose de retarget como asset

#pragma once

#include "CoreMinimal.h"

class FIKRetargetEditor;
class UToolMenu;

struct FToolMenuContext;

/**
 * As duas pontas do UFofuxoPoseDeRetarget: gravar a pose que esta aberta num
 * asset, e trazer de volta a pose de um asset.
 *
 * Moram no mesmo menu do "Copiar pose", numa secao propria, porque sao a mesma
 * pergunta com outro alcance: aquele copia de outro retargeter *deste* projeto,
 * este copia de um arquivo, que atravessa projeto.
 */
class FFofuxoPoseNoDisco
{
public:
	/** Poe a secao "Do disco" no menu do botao Copiar pose. */
	static void MontarSecao(UToolMenu* Menu);

private:
	/** O editor de retarget dono desta barra, ou nullptr se nao for um. */
	static FIKRetargetEditor* EditorDoContexto(const FToolMenuContext& Contexto);

	/** Pergunta onde salvar e grava a pose do lado que esta sendo editado. */
	static void Salvar(const FToolMenuContext& Contexto);

	/** Pergunta qual asset e substitui por ele a pose do lado que esta sendo editado. */
	static void Aplicar(const FToolMenuContext& Contexto);
};
