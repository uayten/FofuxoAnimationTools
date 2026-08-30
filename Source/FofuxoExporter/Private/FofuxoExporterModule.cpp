// Fofuxo

#include "SFofuxoExportWindow.h"
#include "FofuxoNome.h"

#include "Algo/AnyOf.h"
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetData.h"
#include "ContentBrowserMenuContexts.h"
#include "Engine/SkeletalMesh.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FofuxoExporter"

class FFofuxoExporterModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FFofuxoExporterModule::RegistrarMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

private:
	void RegistrarMenus()
	{
		FToolMenuOwnerScoped Dono(this);

		// O Export... e o Migrate... nao ficam no menu de contexto de cima: eles
		// sao montados pelo MakeAssetActionsSubMenu, dentro do submenu
		// "Asset Actions". A secao chama AssetContextMoveActions.
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.AssetActionsSubMenu");
		if (Menu == nullptr)
		{
			return;
		}

		FToolMenuSection& Secao = Menu->FindOrAddSection("AssetContextMoveActions");

		Secao.AddDynamicEntry("FofuxoExport", FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& Entrada)
			{
				const UContentBrowserAssetContextMenuContext* Contexto =
					Entrada.FindContext<UContentBrowserAssetContextMenuContext>();

				if (Contexto == nullptr)
				{
					return;
				}

				// Aparece tanto com animacao quanto com malha selecionada. Com
				// so a malha, a janela junta as animacoes do esqueleto dela.
				const bool bServe = Algo::AnyOf(Contexto->SelectedAssets,
					[](const FAssetData& Asset)
					{
						return Asset.AssetClassPath == UAnimSequence::StaticClass()->GetClassPathName()
							|| Asset.AssetClassPath == USkeletalMesh::StaticClass()->GetClassPathName();
					});

				if (!bServe)
				{
					return;
				}

				const TArray<FAssetData> Selecionados = Contexto->SelectedAssets;

				FToolMenuEntry Item = FToolMenuEntry::InitMenuEntry(
					"FofuxoExport",
					FText::Format(LOCTEXT("FofuxoExport", "{0} -- Exportar"), Fofuxo::NomeCurto()),
					LOCTEXT("FofuxoExportTooltip",
						"Exporta as animacoes e um Skeletal Mesh para um unico FBX, "
						"cada animacao como um take com o nome do asset. Sem nenhuma "
						"animacao marcada, sai so a malha."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([Selecionados]()
					{
						SFofuxoExportWindow::Abrir(Selecionados);
					})));

				Item.InsertPosition = FToolMenuInsert("Export", EToolMenuInsertType::After);

				Entrada.AddEntry(Item);
			}));
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFofuxoExporterModule, FofuxoExporter)
