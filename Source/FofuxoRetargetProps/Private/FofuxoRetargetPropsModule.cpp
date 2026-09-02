// Fofuxo -- the IK Retargeter tools' module
//
// Startup and shutdown only. Everything the plugin does in the retarget editor
// lives in one FofuxoXxx.h/.cpp pair each -- the toolbar in FofuxoToolbar, the
// preview attachments in Attachments/FofuxoAttachments, and so on.
//
// The order in Shutdown is not decoration: the widgets, the modes and the
// customizations all live in this DLL, and anything left registered after the
// unload -- a Live Coding pass, for instance -- would call code that no longer
// exists.

#include "FofuxoAttachmentDetails.h"
#include "FofuxoAttachments.h"
#include "FofuxoBoneDetails.h"
#include "FofuxoBonesOnScreen.h"
#include "FofuxoLiveRetarget.h"
#include "FofuxoMirrorPose.h"
#include "FofuxoResetRotation.h"
#include "FofuxoSourceViewport.h"
#include "FofuxoToolbar.h"

#include "Modules/ModuleManager.h"
#include "ToolMenus.h"

class FFofuxoRetargetPropsModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
		Mirror = MakeUnique<FFofuxoMirrorPose>();
		Mirror->Start();

		Attachments = MakeUnique<FFofuxoAttachments>();
		Attachments->Tell(Mirror.Get());
		Attachments->Start();

		FFofuxoAttachmentDetails::Register();
		FFofuxoLiveRetarget::Register();
		FFofuxoBoneDetails::Register();
		FFofuxoResetRotation::Register();

		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FFofuxoRetargetPropsModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);

		FFofuxoAttachmentDetails::Forget();
		FFofuxoLiveRetarget::Forget();
		FFofuxoBoneDetails::Forget();
		FFofuxoResetRotation::Forget();
		FFofuxoSourceViewport::Forget();
		FFofuxoBonesOnScreen::Forget();

		if (Attachments.IsValid())
		{
			Attachments->Stop();
			Attachments.Reset();
		}

		if (Mirror.IsValid())
		{
			Mirror->Stop();
			Mirror.Reset();
		}
	}

private:

	void RegisterMenus()
	{
		FFofuxoToolbar::Register(this, Mirror.Get());
	}

	TUniquePtr<FFofuxoAttachments> Attachments;
	TUniquePtr<FFofuxoMirrorPose> Mirror;
};

IMPLEMENT_MODULE(FFofuxoRetargetPropsModule, FofuxoRetargetProps)
