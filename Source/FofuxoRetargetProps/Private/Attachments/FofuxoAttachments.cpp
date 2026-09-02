// Fofuxo -- hanging the preview attachments in the retarget viewport

#include "FofuxoAttachments.h"

#include "FofuxoAttachmentsOp.h"
#include "FofuxoBonesOnScreen.h"
#include "FofuxoLiveRetarget.h"
#include "FofuxoMirrorPose.h"
#include "FofuxoRedoRetarget.h"
#include "FofuxoResetRotation.h"
#include "FofuxoSourceViewport.h"

#include "Animation/DebugSkelMeshComponent.h"
#include "ComponentAssetBroker.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Retargeter/IKRetargeter.h"
#include "RetargetEditor/IKRetargetEditor.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "StructUtils/InstancedStruct.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"

namespace FofuxoAttachments
{
	/** FAssetEditorToolkit::GetEditorName() of the retarget editor. */
	static const FName EditorName("IKRetargetEditor");

	/** The attachment list stored in this retargeter, or nullptr if it has no op. */
	static const FFofuxoAttachmentsOpSettings* AttachmentsOfRetargeter(const UIKRetargeter* Retargeter)
	{
		if (Retargeter == nullptr)
		{
			return nullptr;
		}

		for (const FInstancedStruct& Op : Retargeter->GetRetargetOps())
		{
			if (const FFofuxoAttachmentsOp* Ours = Op.GetPtr<FFofuxoAttachmentsOp>())
			{
				return &Ours->Settings;
			}
		}

		return nullptr;
	}

	/**
	 * A number that changes when the list changes.
	 *
	 * Editing the list in the details panel tells nobody, and rebuilding the
	 * components every half second would make the weapon flicker. So the tick
	 * compares this summary with the previous walk's.
	 *
	 * Enable Op goes in here, and not into a separate "if": turning the op off is
	 * a change like any other, and the same mechanism that reacts to a new row
	 * reacts to it -- it releases everything and hangs nothing back.
	 */
	static uint32 SignatureOfAttachments(const UIKRetargeter* Retargeter)
	{
		const FFofuxoAttachmentsOpSettings* List = AttachmentsOfRetargeter(Retargeter);
		if (List == nullptr || !List->bEnabled)
		{
			return 0;
		}

		uint32 Summary = GetTypeHash(List->Attachments.Num());

		for (const FFofuxoAttachment& Attachment : List->Attachments)
		{
			Summary = HashCombine(Summary, GetTypeHash(Attachment.bShow));
			Summary = HashCombine(Summary, GetTypeHash(Attachment.SourceBone.BoneName));
			Summary = HashCombine(Summary, GetTypeHash(Attachment.TargetBone.BoneName));
			Summary = HashCombine(Summary, GetTypeHash(Attachment.Asset.ToString()));
			Summary = HashCombine(Summary, GetTypeHash(static_cast<uint8>(Attachment.Character)));
			// By its text, and not by its components: FRotator has no GetTypeHash,
			// and this list is half a dozen rows checked twice a second.
			Summary = HashCombine(Summary, GetTypeHash(Attachment.PreviewFit.ToString()));
		}

		return Summary;
	}
}

void FFofuxoAttachments::Start()
{
	Ticker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FFofuxoAttachments::Tick), 0.5f);
}

void FFofuxoAttachments::Stop()
{
	if (Ticker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
		Ticker.Reset();
	}

	for (FOpenEditor& One : Open)
	{
		Release(One);
	}
	Open.Reset();

	FFofuxoRedoRetarget::Forget();
}

bool FFofuxoAttachments::Tick(float)
{
	if (GEditor == nullptr)
	{
		return true;
	}

	UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (Subsystem == nullptr)
	{
		return true;
	}

	// A closed editor takes the whole preview scene with it, so there is nothing
	// to release -- only the entry to drop.
	Open.RemoveAll([](const FOpenEditor& One) { return !One.Asset.IsValid(); });

	for (UObject* Edited : Subsystem->GetAllEditedAssets())
	{
		UIKRetargeter* Retargeter = Cast<UIKRetargeter>(Edited);
		if (Retargeter == nullptr)
		{
			continue;
		}

		IAssetEditorInstance* Instance = Subsystem->FindEditorForAsset(Edited, false);
		if (Instance == nullptr || Instance->GetEditorName() != FofuxoAttachments::EditorName)
		{
			continue;
		}

		// GetEditorName() only returns that name coming from FIKRetargetEditor,
		// which is an FAssetEditorToolkit. Both steps of the cast are single
		// inheritance.
		FAssetEditorToolkit* Toolkit = static_cast<FAssetEditorToolkit*>(Instance);
		FIKRetargetEditor* Editor = static_cast<FIKRetargetEditor*>(Toolkit);

		FOpenEditor* One = Open.FindByPredicate(
			[Retargeter](const FOpenEditor& Candidate) { return Candidate.Asset == Retargeter; });

		if (One == nullptr)
		{
			One = &Open.AddDefaulted_GetRef();
			One->Asset = Retargeter;
		}

		Sync(*One, *Editor);

		// Riding along on the same walk through the open editors: the redo button
		// can also only be placed once its tab exists, and the mirror needs to know
		// this editor opened.
		FFofuxoRedoRetarget::EnsureButton(*Editor);
		FFofuxoLiveRetarget::Follow(*Editor);
		FFofuxoResetRotation::EnsureShortcut(*Editor);
		FFofuxoSourceViewport::EnsureTab(*Editor);
		FFofuxoBonesOnScreen::Follow(*Editor);

		if (Mirror != nullptr)
		{
			Mirror->Follow(*Editor);
		}
	}

	// After everyone: the undo notice is good for one walk only.
	FFofuxoLiveRetarget::ClearUndoNotice();

	return true;
}

void FFofuxoAttachments::Sync(FOpenEditor& One, FIKRetargetEditor& Editor)
{
	const TSharedRef<FIKRetargetEditorController> Controller = Editor.GetController();

	UDebugSkelMeshComponent* Source = Controller->SourceSkelMeshComponent;
	UDebugSkelMeshComponent* Target = Controller->TargetSkelMeshComponent;

	USkeletalMesh* SourceMesh = Source ? Source->GetSkeletalMeshAsset() : nullptr;
	USkeletalMesh* TargetMesh = Target ? Target->GetSkeletalMeshAsset() : nullptr;

	const uint32 Signature = FofuxoAttachments::SignatureOfAttachments(One.Asset.Get());

	const bool bChanged =
		One.SourceComponent != Source ||
		One.TargetComponent != Target ||
		One.SourceMesh != SourceMesh ||
		One.TargetMesh != TargetMesh ||
		One.Signature != Signature;

	if (!bChanged)
	{
		return;
	}

	Release(One);
	HangFromOp(One, One.Asset.Get(), Source, Target);

	One.Signature = Signature;
	One.SourceComponent = Source;
	One.TargetComponent = Target;
	One.SourceMesh = SourceMesh;
	One.TargetMesh = TargetMesh;
}

void FFofuxoAttachments::HangFromOp(
	FOpenEditor& One,
	const UIKRetargeter* Retargeter,
	UDebugSkelMeshComponent* Source,
	UDebugSkelMeshComponent* Target)
{
	const FFofuxoAttachmentsOpSettings* List = FofuxoAttachments::AttachmentsOfRetargeter(Retargeter);
	if (List == nullptr || !List->bEnabled)
	{
		return;
	}

	for (const FFofuxoAttachment& Attachment : List->Attachments)
	{
		if (!Attachment.bShow)
		{
			continue;
		}

		// LoadSynchronous, and not Get: the weapon may not be loaded, and this is
		// the moment we find out it is needed. Once only, even when it goes on both
		// characters.
		UObject* Object = Attachment.Asset.LoadSynchronous();
		if (Object == nullptr)
		{
			continue;
		}

		// One bone per side: the two skeletons almost never call the same bone by
		// the same name, and that is why retargeting exists.
		if (Attachment.Character != EFofuxoCharacter::Target)
		{
			HangOn(One, Source, Object, Attachment, Attachment.SourceBone.BoneName);
		}

		if (Attachment.Character != EFofuxoCharacter::Source)
		{
			HangOn(One, Target, Object, Attachment, Attachment.TargetBone.BoneName);
		}
	}
}

void FFofuxoAttachments::HangOn(
	FOpenEditor& One,
	UDebugSkelMeshComponent* Component,
	UObject* Object,
	const FFofuxoAttachment& Attachment,
	const FName Bone)
{
	if (Component == nullptr)
	{
		return;
	}

	UWorld* World = Component->GetWorld();
	AWorldSettings* Owner = World ? World->GetWorldSettings() : nullptr;
	if (Owner == nullptr)
	{
		return;
	}

	// Attach goes quiet when the bone is empty or doesn't exist on this side -- a
	// half-filled row is not an error, it just doesn't show up.
	Attach(One, Component, Owner, Object, Bone, Attachment.PreviewFit);
}

void FFofuxoAttachments::Attach(
	FOpenEditor& One,
	UDebugSkelMeshComponent* Component,
	AWorldSettings* Owner,
	UObject* Object,
	const FName Socket,
	const FTransform& Fit)
{
	if (Object == nullptr || Socket.IsNone())
	{
		return;
	}

	// DoesSocketExist covers sockets and bones. The attachment point may not exist
	// on this side of the retarget: the target's skeleton rarely has the same names
	// as the source's.
	if (!Component->DoesSocketExist(Socket))
	{
		return;
	}

	const TSubclassOf<UActorComponent> Class =
		FComponentAssetBrokerage::GetPrimaryComponentForAsset(Object->GetClass());

	if (!*Class || !Class->IsChildOf(USceneComponent::StaticClass()))
	{
		return;
	}

	// RF_Transient, and not RF_Transactional as in Persona: these are derived from
	// what is already saved in the op, there is nothing to undo and nothing to
	// write.
	USceneComponent* Attachment = NewObject<USceneComponent>(Owner, Class, NAME_None, RF_Transient);

	FComponentAssetBrokerage::AssignAssetToComponent(Attachment, Object);

	Attachment->SetupAttachment(Component, Socket);
	Attachment->RegisterComponent();

	if (!Fit.Equals(FTransform::Identity))
	{
		Attachment->SetRelativeTransform(Fit);
	}

	One.Attachments.Add(Attachment);
}

void FFofuxoAttachments::Release(FOpenEditor& One)
{
	for (const TWeakObjectPtr<USceneComponent>& Weak : One.Attachments)
	{
		if (USceneComponent* Attachment = Weak.Get())
		{
			Attachment->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
			Attachment->DestroyComponent();
		}
	}

	One.Attachments.Reset();
}
