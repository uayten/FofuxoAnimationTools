// Fofuxo -- cena glTF
//
// Escreve um esqueleto e varias animacoes num arquivo glTF so.
//
// Aqui o formato faz o trabalho: glTF tem um array "animations" nativo, com
// nome, sobre um unico "skin". Nao e biblioteca que o outro lado precise saber
// interpretar -- e o desenho do formato, e o importador que vem dentro do
// Blender cria uma acao para cada.
//
// O exportador da Epic escreve uma animacao por arquivo, mas por escolha dele:
// o builder aceita quantas voce somar. E o que se faz aqui.

#include "FofuxoCenaUsd.h"

#include "Animation/AnimSequence.h"
#include "Builders/GLTFContainerBuilder.h"
#include "Engine/SkeletalMesh.h"
#include "Json/GLTFJsonNode.h"
#include "Json/GLTFJsonScene.h"
#include "Json/GLTFJsonSkin.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Options/GLTFExportOptions.h"
#include "UObject/GCObjectScopeGuard.h"

#define LOCTEXT_NAMESPACE "FofuxoGltfCena"

namespace FofuxoGltf
{
	static bool Escrever(const FFofuxoPedidoDeCena& Pedido, FText& OutErro)
	{
		// Lista vazia sai um arquivo so com o skin e a malha, e o array
		// "animations" nem aparece -- que e o pedido de "so a malha".
		if (Pedido.Malha == nullptr)
		{
			OutErro = LOCTEXT("GltfSemMalha", "Sem Skeletal Mesh nao da para escrever o esqueleto.");
			return false;
		}

		UGLTFExportOptions* Opcoes = NewObject<UGLTFExportOptions>();
		FGCObjectScopeGuard Guarda(Opcoes);

		// glTF e sempre metros e Y para cima -- nao ha eixo nem unidade a
		// escolher, e e justamente por isso que ele viaja bem. O 0,01 e a
		// conversao de centimetro da Unreal para metro; a escala do destino
		// multiplica em cima.
		Opcoes->ExportUniformScale = 0.01f * static_cast<float>(Pedido.Escala);
		Opcoes->bExportVertexSkinWeights = true;
		Opcoes->bExportAnimationSequences = Pedido.Animacoes.Num() > 0;

		FGLTFContainerBuilder Construtor(FPaths::GetCleanFilename(Pedido.Caminho), Opcoes);
		Construtor.ClearLog();

		FGLTFJsonNode* No = Construtor.AddNode();
		if (No == nullptr)
		{
			OutErro = LOCTEXT("GltfSemNo", "Nao consegui criar o no raiz do glTF.");
			return false;
		}

		No->Name = Pedido.Malha->GetName();

		// O skin sai uma vez, e todas as animacoes se penduram nele. E este o
		// "um esqueleto, varias animacoes" que o FBX faz com takes.
		No->Skin = Construtor.AddUniqueSkin(No, Pedido.Malha);
		if (No->Skin == nullptr)
		{
			OutErro = FText::Format(
				LOCTEXT("GltfSemSkin", "A engine nao converteu o esqueleto de {0}."),
				FText::FromString(Pedido.Malha->GetName()));
			return false;
		}

		if (Pedido.bComMalha)
		{
			No->Mesh = Construtor.AddUniqueMesh(Pedido.Malha);
		}

		for (UAnimSequence* Sequencia : Pedido.Animacoes)
		{
			if (Sequencia == nullptr)
			{
				continue;
			}

			if (Construtor.AddUniqueAnimation(No, Pedido.Malha, Sequencia) == nullptr)
			{
				OutErro = FText::Format(
					LOCTEXT("GltfAnimacaoFalhou", "A engine nao converteu a animacao {0}."),
					FText::FromString(Sequencia->GetName()));
				return false;
			}
		}

		FGLTFJsonScene* Cena = Construtor.AddScene();
		if (Cena == nullptr)
		{
			OutErro = LOCTEXT("GltfSemCena", "Nao consegui criar a cena do glTF.");
			return false;
		}

		Cena->Nodes.Add(No);
		Construtor.DefaultScene = Cena;

		// O builder tem fila de tarefas com progresso proprio -- e por isso que a
		// conversao pesada aqui nao deixa o editor mudo como o FBX deixava.
		Construtor.ProcessSlowTasks();

		if (!Construtor.WriteAllFiles(FPaths::GetPath(Pedido.Caminho)))
		{
			OutErro = FText::Format(
				LOCTEXT("GltfNaoEscreveu", "O arquivo {0} nao foi escrito. O Output Log tem o motivo."),
				FText::FromString(FPaths::GetCleanFilename(Pedido.Caminho)));
			return false;
		}

		return true;
	}
}

class FFofuxoGltfCenaModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FofuxoEscritorDeCenaGltf().BindStatic(&FofuxoGltf::Escrever);
	}

	virtual void ShutdownModule() override
	{
		FofuxoEscritorDeCenaGltf().Unbind();
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFofuxoGltfCenaModule, FofuxoGltfCena)
