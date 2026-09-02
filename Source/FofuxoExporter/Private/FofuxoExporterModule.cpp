// Fofuxo

#include "FofuxoExportFlow.h"
#include "FofuxoName.h"

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
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FFofuxoExporterModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);
	}

private:

	void RegisterMenus()
	{
		FToolMenuOwnerScoped Owner(this);

		// Export... and Migrate... are not on the top context menu: they are
		// built by MakeAssetActionsSubMenu, inside the "Asset Actions" submenu.
		// The section is called AssetContextMoveActions.
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("ContentBrowser.AssetContextMenu.AssetActionsSubMenu");
		if (Menu == nullptr)
		{
			return;
		}

		FToolMenuSection& Section = Menu->FindOrAddSection("AssetContextMoveActions");

		Section.AddDynamicEntry("FofuxoExport", FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& Entry)
			{
				const UContentBrowserAssetContextMenuContext* Context =
					Entry.FindContext<UContentBrowserAssetContextMenuContext>();

				if (Context == nullptr)
				{
					return;
				}

				// It shows up both with an animation and with a mesh selected.
				// With only the mesh, the window gathers its skeleton's
				// animations.
				const bool bFits = Algo::AnyOf(Context->SelectedAssets,
					[](const FAssetData& Asset)
					{
						return Asset.AssetClassPath == UAnimSequence::StaticClass()->GetClassPathName()
							|| Asset.AssetClassPath == USkeletalMesh::StaticClass()->GetClassPathName();
					});

				if (!bFits)
				{
					return;
				}

				const TArray<FAssetData> Selected = Context->SelectedAssets;

				FToolMenuEntry Item = FToolMenuEntry::InitMenuEntry(
					"FofuxoExport",
					FText::Format(LOCTEXT("FofuxoExport", "{0} -- Export"), Fofuxo::ShortName()),
					LOCTEXT("FofuxoExportTooltip",
						"Exports the animations and a Skeletal Mesh into a single file, "
						"each animation as a take named after the asset. With no animation "
						"ticked, only the mesh comes out."),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateLambda([Selected]()
					{
						FFofuxoExportFlow::Run(Selected);
					})));

				Item.InsertPosition = FToolMenuInsert("Export", EToolMenuInsertType::After);

				Entry.AddEntry(Item);
			}));
	}
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFofuxoExporterModule, FofuxoExporter)
