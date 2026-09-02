// Fofuxo -- the section on the IK Retargeter's toolbar

#include "FofuxoToolbar.h"

#include "FofuxoAlignBones.h"
#include "FofuxoBonesOnScreen.h"
#include "FofuxoCopyPose.h"
#include "FofuxoLiveRetarget.h"
#include "FofuxoMirrorPose.h"
#include "FofuxoSourceViewport.h"

#include "Framework/Commands/UIAction.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "ToolMenuContext.h"
#include "ToolMenuSection.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

namespace FofuxoToolbar
{
	// FAssetEditorToolkit::GetEditorName() of the retarget editor.
	static const FName EditorName("IKRetargetEditor");

	// FWorkflowCentricApplication glues the mode's name onto the end of the
	// toolbar's: "AssetEditor." + GetToolMenuAppName() + ".ToolBar" + "." + mode.
	static const FName ToolbarName("AssetEditor.IKRetargetEditor.ToolBar.IKRetargetApplicationMode");
}

FIKRetargetEditor* FFofuxoToolbar::EditorOfContext(const FToolMenuContext& Context)
{
	const UAssetEditorToolkitMenuContext* FromEditor = Context.FindContext<UAssetEditorToolkitMenuContext>();
	if (FromEditor == nullptr)
	{
		return nullptr;
	}

	const TSharedPtr<FAssetEditorToolkit> Toolkit = FromEditor->Toolkit.Pin();
	if (!Toolkit.IsValid() || Toolkit->GetEditorName() != FofuxoToolbar::EditorName)
	{
		return nullptr;
	}

	return static_cast<FIKRetargetEditor*>(Toolkit.Get());
}

void FFofuxoToolbar::Register(void* Owner, FFofuxoMirrorPose* Mirror)
{
	FToolMenuOwnerScoped OwnerScope(Owner);

	UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu(FofuxoToolbar::ToolbarName);
	if (Toolbar == nullptr)
	{
		return;
	}

	FToolMenuSection& Section = Toolbar->FindOrAddSection("Fofuxo");

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		"FofuxoLiveRetarget",
		FUIAction(
			FExecuteAction::CreateStatic(&FFofuxoLiveRetarget::Toggle),
			FCanExecuteAction(),
			FIsActionChecked::CreateStatic(&FFofuxoLiveRetarget::IsOn)),
		LOCTEXT("LiveRetarget", "Live Retarget"),
		LOCTEXT("LiveRetargetTip",
			"Puts a rotation gizmo in Running Retarget: with the animation paused on whichever frame "
			"you like, click a target bone and turn it. Made for fingers, which in the ref pose are "
			"open and don't show whether they close around the weapon.\n\n"
			"Target only, and only with the Source/Target button on the target -- the source's "
			"animation is the input, there is nothing to adjust in it.\n\n"
			"What the gizmo writes is the retarget pose, not a fix for that frame: the retargeter has "
			"nowhere to store a per-frame correction. But the turn you see on frame 37 is the same turn "
			"that comes out on all the others -- for fingers that is correct, because the error of a "
			"finger gripping a sword is constant and the frame is only there so you can see it."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.RotateMode"),
		EUserInterfaceActionType::ToggleButton));

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		"FofuxoSourceViewport",
		FToolUIActionChoice(FToolUIAction(
			FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& Context)
			{
				if (FIKRetargetEditor* Editor = FFofuxoToolbar::EditorOfContext(Context))
				{
					FFofuxoSourceViewport::Open(*Editor);
				}
			}))),
		LOCTEXT("SourceViewport", "Source viewport"),
		LOCTEXT("SourceViewportTip",
			"Opens a second viewport onto the same scene, with the camera locked to the source bone "
			"matching the selected bone -- and following it frame by frame while the animation "
			"runs.\n\n"
			"It is for seeing the reference and the adjustment at once: in one viewport you turn the "
			"target's finger, in the other you see where the Manny's finger is on that same frame, "
			"without flying the camera between the two characters.\n\n"
			"The counterpart comes from the chain mapping. The tab is also in Window, and it doesn't "
			"come back on its own when the editor reopens."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Viewports")));

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		"FofuxoStickBones",
		FUIAction(
			FExecuteAction::CreateStatic(&FFofuxoBonesOnScreen::Toggle),
			FCanExecuteAction(),
			FIsActionChecked::CreateStatic(&FFofuxoBonesOnScreen::IsOn)),
		LOCTEXT("StickBones", "Stick bones"),
		LOCTEXT("StickBonesTip",
			"Swaps Unreal's octahedral bone for a stick like Blender's: a thin line between the "
			"joints and a circle on each, the same size on screen at any camera distance. On a hand "
			"with fifteen bones it is the difference between seeing the fingers and seeing a grey "
			"ball.\n\n"
			"The engine's drawing doesn't vanish, it only shrinks -- the bone's identity for clicking "
			"lives in it. It stays hidden underneath the stick.\n\n"
			"The shrunken size is the retargeter's BoneDrawSize, the same as the slider under "
			"Character > Bones. Turning it off restores the previous value; saving the RTG with this "
			"on stores the shrunken size."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorViewport.WireframeMode"),
		EUserInterfaceActionType::ToggleButton));

	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		"FofuxoMirror",
		FUIAction(
			FExecuteAction::CreateLambda([Mirror]() { Mirror->Toggle(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([Mirror]() { return Mirror->IsOn(); })),
		LOCTEXT("Mirror", "Mirror"),
		LOCTEXT("MirrorTip",
			"Repeats on the bone of the other side the rotation you give a bone -- turn thigh_l and "
			"thigh_r follows, mirrored. It works in Editing Retarget Pose and in Live Retarget too, "
			"with the gizmo or with Alt+R.\n\n"
			"It finds the pair by name: the side written as l/r, left/right or lt/rt, separated by "
			"\"_\", \".\", \"-\" or a space, in any case, or glued on in camelCase (HandL). A bone with "
			"no pair, like the spine and the head, stays out.\n\n"
			"If you move both sides at once -- both selected in the gizmo, a global Auto Align, a "
			"Ctrl+Z -- the mirror stays out of it."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "UMGEditor.Mirror"),
		EUserInterfaceActionType::ToggleButton));

	// Label, tooltip and icon are attributes, and not fixed text: they change with
	// the armed mode, which is how the toolbar shows what the click will do.
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		"FofuxoAlign",
		FToolUIActionChoice(FToolUIAction(
			FToolMenuExecuteAction::CreateStatic(&FFofuxoAlignBones::Align),
			FToolMenuCanExecuteAction::CreateStatic(&FFofuxoAlignBones::Can),
			FToolMenuGetActionCheckState())),
		TAttribute<FText>::CreateLambda(
			[]() { return FFofuxoAlignBones::Label(FFofuxoAlignBones::Mode()); }),
		TAttribute<FText>::CreateLambda([]()
		{
			return FText::Format(
				LOCTEXT("AlignTipWithMode", "{0}\n\nIn the three dots beside it you change what this button does."),
				FFofuxoAlignBones::Tooltip(FFofuxoAlignBones::Mode()));
		}),
		TAttribute<FSlateIcon>::CreateLambda(
			[]() { return FFofuxoAlignBones::Icon(FFofuxoAlignBones::Mode()); })));

	Section.AddEntry(FToolMenuEntry::InitComboButton(
		"FofuxoAlignOptions",
		FToolUIActionChoice(),
		FNewToolMenuChoice(FNewToolMenuDelegate::CreateStatic(&FFofuxoAlignBones::BuildModeMenu)),
		FText::GetEmpty(),
		LOCTEXT("AlignOptionsTip", "Choose what the Align button does."),
		FSlateIcon(),
		/*bSimpleComboBox*/ true));

	Section.AddEntry(FToolMenuEntry::InitComboButton(
		"FofuxoCopyPose",
		FToolUIActionChoice(FToolUIAction(
			FToolMenuExecuteAction(),
			FToolMenuCanExecuteAction::CreateStatic(&FFofuxoCopyPose::Can),
			FToolMenuGetActionCheckState())),
		FNewToolMenuChoice(FNewToolMenuDelegate::CreateStatic(&FFofuxoCopyPose::BuildMenu)),
		LOCTEXT("CopyPose", "Copy pose"),
		LOCTEXT("CopyPoseToolbarTip",
			"Brings another retargeter's retarget pose into the side you are editing, matching bones "
			"by name.\n\n"
			"It is for the fix that doesn't travel: if every retarget in the project starts from the "
			"same character, the source side of all of them has the same pose, and adjusting one "
			"doesn't adjust the others. On the target side the same holds between characters "
			"following Unreal's naming convention.\n\n"
			"The pose here is replaced, not added to, and the question before writing says how many "
			"bones match, how many are left out and how many go back to the ref pose."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Clipboard")));
}

#undef LOCTEXT_NAMESPACE
