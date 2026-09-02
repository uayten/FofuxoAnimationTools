// Fofuxo -- from the selection to the file on disk

#include "FofuxoExportFlow.h"
#include "FofuxoName.h"

#include "FofuxoAnimationScan.h"
#include "FofuxoExportOptions.h"
#include "FofuxoExportRequest.h"
#include "FofuxoFbxWriter.h"
#include "FofuxoSceneWriter.h"
#include "SFofuxoExportWindow.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "FofuxoExporter"

void FFofuxoExportFlow::Run(const TArray<FAssetData>& Selected)
{
	TArray<UAnimSequence*> Animations;
	USkeletalMesh* Mesh = nullptr;

	for (const FAssetData& Asset : Selected)
	{
		if (UObject* Loaded = Asset.GetAsset())
		{
			if (UAnimSequence* Sequence = Cast<UAnimSequence>(Loaded))
			{
				Animations.Add(Sequence);
			}
			else if (USkeletalMesh* Picked = Cast<USkeletalMesh>(Loaded))
			{
				// If more than one mesh is selected, the first one wins.
				if (Mesh == nullptr)
				{
					Mesh = Picked;
				}
			}
		}
	}

	// With no mesh in the selection, try the preview mesh of the animations'
	// skeleton.
#if WITH_EDITORONLY_DATA
	if (Mesh == nullptr && Animations.Num() > 0)
	{
		if (USkeleton* Skeleton = Animations[0]->GetSkeleton())
		{
			Mesh = Skeleton->GetPreviewMesh();
		}
	}
#endif

	// Only the mesh was clicked: gather every animation of its skeleton. They
	// come in ticked, but the list shows up before exporting, so they can be
	// unticked.
	if (Animations.Num() == 0 && Mesh != nullptr)
	{
		Animations = FofuxoAnimationsOfSkeleton(Mesh->GetSkeleton());
	}

	// No mesh and no animation, and there is nothing to export. Mesh and no
	// animation, and there is: the window opens with an empty list and a file
	// comes out with just the mesh and the skeleton -- which is the case of
	// someone who only wants the geometry on the other side.
	if (Animations.Num() == 0 && Mesh == nullptr)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NothingToExport", "Select at least one Animation Sequence or one Skeletal Mesh."));
		return;
	}

	// Alphabetical order so the takes always come out in the same order, and not
	// in the order you happened to click.
	Animations.Sort([](const UAnimSequence& A, const UAnimSequence& B)
	{
		return A.GetName() < B.GetName();
	});

	UFofuxoExportOptions* Options = NewObject<UFofuxoExportOptions>();
	TStrongObjectPtr<UFofuxoExportOptions> Guard(Options);

	Options->LoadConfig();
	Options->ApplyTarget();
	Options->SkeletalMesh = Mesh;

	if (Options->Folder.Path.IsEmpty())
	{
		Options->Folder.Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Exported"));
	}

	Options->FileName = Mesh != nullptr ? Mesh->GetName() : Animations[0]->GetName();

	const TSharedRef<SWindow> NewWindow = SNew(SWindow)
		.Title(FText::Format(LOCTEXT("WindowTitle", "{0} -- Export"), Fofuxo::Name()))
		.ClientSize(FVector2D(600.f, 700.f))
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	const TSharedRef<SFofuxoExportWindow> Content = SNew(SFofuxoExportWindow)
		.Window(NewWindow)
		.Options(Options);

	// Whatever you unticked last time comes back unticked.
	Content->SetAnimations(Animations, TSet<FString>(Options->Unticked));

	NewWindow->SetContent(Content);
	FSlateApplication::Get().AddModalWindow(NewWindow, nullptr);

	if (!Content->Confirmed())
	{
		return;
	}

	// Store the ticks without losing other characters': it only touches the
	// animations that were on this list.
	{
		TSet<FString> Updated(Options->Unticked);
		Content->CollectTickState(Updated);
		Options->Unticked = Updated.Array();
	}

	Options->bListExpanded = Content->IsListExpanded();
	Options->ListHeight = Content->GetListHeight();

	Options->SaveConfig();

	// SaveConfig only marks the ini as dirty -- without this it sits waiting for
	// a clean editor shutdown, and a session that ends any other way loses the
	// folder and the ticks.
	if (GConfig != nullptr)
	{
		// With no file name it writes every dirty ini. That avoids depending on
		// translating the config class name into a file path.
		GConfig->Flush(false);
	}

	FFofuxoExportRequest Request;
	Request.Animations = Content->Ticked();
	Request.SkeletalMesh = Options->SkeletalMesh.LoadSynchronous();
	Request.FilePath = Options->BuildPath();
	Request.Options = Options;

	const double Started = FPlatformTime::Seconds();

	FText Error;

	const bool bWorked = Options->Format == EFofuxoFormat::FBX
		? FFofuxoFbxWriter::Export(Request, Error)
		: FFofuxoSceneWriter::Export(Request, Error);

	if (!bWorked)
	{
		FMessageDialog::Open(EAppMsgType::Ok, Error);
		return;
	}

	// The time in the notification is what answers "is this normal or did it
	// hang?" next time: with the progress bar the wait stops being blind, and
	// with its number there is something to compare against.
	const int32 Seconds = FMath::RoundToInt(FPlatformTime::Seconds() - Started);

	const FText Time = Seconds >= 60
		? FText::Format(LOCTEXT("MinSec", "{0} min {1} s"), FText::AsNumber(Seconds / 60), FText::AsNumber(Seconds % 60))
		: FText::Format(LOCTEXT("Sec", "{0} s"), FText::AsNumber(Seconds));

	const FText Folder = FText::FromString(FPaths::GetCleanFilename(FPaths::GetPath(Request.FilePath)));

	FNotificationInfo Notice(Request.Animations.Num() == 0
		? FText::Format(
			LOCTEXT("ExportedMeshOnly", "{0} exported to {1}, in {2}"),
			FText::FromString(Request.SkeletalMesh != nullptr ? Request.SkeletalMesh->GetName() : FString()),
			Folder,
			Time)
		: FText::Format(
			LOCTEXT("Exported", "{0} animations exported to {1}, in {2}"),
			FText::AsNumber(Request.Animations.Num()),
			Folder,
			Time));

	Notice.ExpireDuration = 6.f;
	Notice.bUseSuccessFailIcons = true;

	const FString FileFolder = FPaths::GetPath(Request.FilePath);
	Notice.Hyperlink = FSimpleDelegate::CreateLambda([FileFolder]()
	{
		FPlatformProcess::ExploreFolder(*FileFolder);
	});
	Notice.HyperlinkText = LOCTEXT("OpenFolder", "Open the folder");

	const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Notice);
	if (Item.IsValid())
	{
		Item->SetCompletionState(SNotificationItem::CS_Success);
	}
}

#undef LOCTEXT_NAMESPACE
