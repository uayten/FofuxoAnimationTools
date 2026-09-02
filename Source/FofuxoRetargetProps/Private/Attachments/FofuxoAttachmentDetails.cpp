// Fofuxo -- the Align button inside the attachments op

#include "FofuxoAttachmentDetails.h"

#include "FofuxoAlignBones.h"
#include "FofuxoAttachmentsOp.h"

#include "DetailWidgetRow.h"
#include "Editor.h"
#include "IDetailChildrenBuilder.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"
#include "ScopedTransaction.h"
#include "StructUtils/InstancedStruct.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

DEFINE_LOG_CATEGORY_STATIC(LogFofuxoAttachmentUI, Log, All);

namespace FofuxoAttachmentUI
{
	/** The row behind the handle, or nullptr if the panel is showing several. */
	static FFofuxoAttachment* RowOfHandle(const TSharedRef<IPropertyHandle>& Handle)
	{
		TArray<void*> Addresses;
		Handle->AccessRawData(Addresses);

		// More than one address is a batch edit, and then there is no "this row's
		// bone".
		return Addresses.Num() == 1 ? static_cast<FFofuxoAttachment*>(Addresses[0]) : nullptr;
	}

	/**
	 * The retargeter that owns this row, found by the row's address.
	 *
	 * The list lives inside the asset, so the open retargeter whose op contains
	 * *this* FFofuxoAttachment is the owner, with no ambiguity possible. The
	 * route through the details panel exists, but it goes through UObject
	 * wrappers with no exported API.
	 */
	static UIKRetargeter* OwnerOfRow(const FFofuxoAttachment* Row)
	{
		if (Row == nullptr || GEditor == nullptr)
		{
			return nullptr;
		}

		UAssetEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (Subsystem == nullptr)
		{
			return nullptr;
		}

		for (UObject* Edited : Subsystem->GetAllEditedAssets())
		{
			UIKRetargeter* Retargeter = Cast<UIKRetargeter>(Edited);
			if (Retargeter == nullptr)
			{
				continue;
			}

			for (const FInstancedStruct& Op : Retargeter->GetRetargetOps())
			{
				const FFofuxoAttachmentsOp* Ours = Op.GetPtr<FFofuxoAttachmentsOp>();
				if (Ours == nullptr)
				{
					continue;
				}

				for (const FFofuxoAttachment& Each : Ours->Settings.Attachments)
				{
					if (&Each == Row)
					{
						return Retargeter;
					}
				}
			}
		}

		return nullptr;
	}

	/** One bone to align, on one side. */
	struct FStep
	{
		ERetargetSourceOrTarget Side = ERetargetSourceOrTarget::Source;
		FName Bone;
		FQuat Delta = FQuat::Identity;
	};

	static FText NameOfSide(const ERetargetSourceOrTarget Side)
	{
		return Side == ERetargetSourceOrTarget::Source
			? LOCTEXT("AttachmentSideSource", "source")
			: LOCTEXT("AttachmentSideTarget", "target");
	}

	static FReply Align(TSharedRef<IPropertyHandle> Handle)
	{
		FFofuxoAttachment* Row = RowOfHandle(Handle);
		UIKRetargeter* Asset = OwnerOfRow(Row);

		if (Row == nullptr || Asset == nullptr)
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT("AttachmentNoOwner",
					"I could not find this row's retargeter. That happens when the panel is showing "
					"more than one attachment at a time -- select just one."));

			return FReply::Handled();
		}

		UIKRetargeterController* Controller = UIKRetargeterController::GetController(Asset);
		if (Controller == nullptr)
		{
			return FReply::Handled();
		}

		TArray<FStep> Steps;

		if (Row->Character != EFofuxoCharacter::Target)
		{
			Steps.Add({ ERetargetSourceOrTarget::Source, Row->SourceBone.BoneName });
		}

		if (Row->Character != EFofuxoCharacter::Source)
		{
			Steps.Add({ ERetargetSourceOrTarget::Target, Row->TargetBone.BoneName });
		}

		TArray<FStep> ToWrite;

		for (FStep& Step : Steps)
		{
			if (Step.Bone.IsNone())
			{
				continue;
			}

			if (FFofuxoAlignBones::DeltaToWorld(
				*Controller, Step.Side, Step.Bone, Row->Axis, Step.Delta))
			{
				ToWrite.Add(Step);
			}
			else
			{
				UE_LOG(LogFofuxoAttachmentUI, Warning,
					TEXT("\"%s\" does not exist in the %s side's skeleton -- left out of the alignment."),
					*Step.Bone.ToString(), *NameOfSide(Step.Side).ToString());
			}
		}

		if (ToWrite.IsEmpty())
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT("AttachmentNoBone",
					"This row has no bone to align: the fields are empty, or the chosen name does not "
					"exist in that side's skeleton. The Output Log says which it was."));

			return FReply::Handled();
		}

		const FScopedTransaction Transaction(LOCTEXT("AttachmentAlignTransaction", "Align to world"));

		Asset->Modify();

		// A single reinitialization, at the end of the scope, even aligning both
		// sides.
		const FScopedReinitializeIKRetargeter Reinitialize(Controller);

		for (const FStep& Step : ToWrite)
		{
			Controller->SetRotationOffsetForRetargetPoseBone(Step.Bone, Step.Delta, Step.Side);

			UE_LOG(LogFofuxoAttachmentUI, Display,
				TEXT("Aligned \"%s\" (%s) to the world axes."),
				*Step.Bone.ToString(), *NameOfSide(Step.Side).ToString());
		}

		return FReply::Handled();
	}

	/**
	 * What the row is, in one line -- for the header, with the row closed.
	 *
	 * "Index [0]" and "6 members" don't say which of the two weapons is which,
	 * which is exactly what you need to know to delete the right one.
	 */
	static FText SummaryOfRow(TSharedRef<IPropertyHandle> Handle)
	{
		const FFofuxoAttachment* Row = RowOfHandle(Handle);
		if (Row == nullptr)
		{
			return FText::GetEmpty();
		}

		const FString Asset = Row->Asset.IsNull()
			? LOCTEXT("AttachmentNoAsset", "no asset").ToString()
			: Row->Asset.GetAssetName();

		// With the attachment on both characters, the bone shown is the target's:
		// that is the skeleton being fixed, and the name that changes from
		// character to character.
		const FName Bone = Row->Character == EFofuxoCharacter::Source
			? Row->SourceBone.BoneName
			: Row->TargetBone.BoneName;

		if (Bone.IsNone())
		{
			return FText::FromString(Asset);
		}

		return FText::Format(
			LOCTEXT("AttachmentSummary", "{0}  on  {1}"),
			FText::FromString(Asset),
			FText::FromName(Bone));
	}

	/** The button's text changes with what it will do: one side, or both. */
	static FText ButtonLabel(TSharedRef<IPropertyHandle> Handle)
	{
		const FFofuxoAttachment* Row = RowOfHandle(Handle);

		if (Row != nullptr && Row->Character == EFofuxoCharacter::Both)
		{
			return LOCTEXT("AttachmentAlignBoth", "Align both to world");
		}

		return LOCTEXT("AttachmentAlignOne", "Align to world");
	}
}

TSharedRef<IPropertyTypeCustomization> FFofuxoAttachmentDetails::Create()
{
	return MakeShared<FFofuxoAttachmentDetails>();
}

void FFofuxoAttachmentDetails::Register()
{
	FPropertyEditorModule& Panel =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	Panel.RegisterCustomPropertyTypeLayout(
		FFofuxoAttachment::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FFofuxoAttachmentDetails::Create));
}

void FFofuxoAttachmentDetails::Forget()
{
	// No LoadModuleChecked: at editor shutdown PropertyEditor may already be
	// gone, and loading it again just to unregister would be worse.
	if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
	{
		FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"))
			.UnregisterCustomPropertyTypeLayout(FFofuxoAttachment::StaticStruct()->GetFName());
	}
}

void FFofuxoAttachmentDetails::CustomizeHeader(
	TSharedRef<IPropertyHandle> Handle,
	FDetailWidgetRow& Row,
	IPropertyTypeCustomizationUtils& Utils)
{
	// The Show checkbox moves up into the header, and that is why it doesn't go
	// into the children list below. It is the only field used with the row
	// closed: hiding one attachment to adjust the other shouldn't force opening
	// both.
	const TSharedPtr<IPropertyHandle> Show =
		Handle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FFofuxoAttachment, bShow));

	Row
		.NameContent()
		[
			Handle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.MinDesiredWidth(260.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				Show.IsValid() ? Show->CreatePropertyValueWidget() : SNullWidget::NullWidget
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([Handle]() { return FofuxoAttachmentUI::SummaryOfRow(Handle); })
				.Font(IPropertyTypeCustomizationUtils::GetRegularFont())
			]
		];
}

void FFofuxoAttachmentDetails::CustomizeChildren(
	TSharedRef<IPropertyHandle> Handle,
	IDetailChildrenBuilder& Builder,
	IPropertyTypeCustomizationUtils& Utils)
{
	uint32 HowMany = 0;
	Handle->GetNumChildren(HowMany);

	static const FName InTheHeader = GET_MEMBER_NAME_CHECKED(FFofuxoAttachment, bShow);

	// One by one, in the order they are declared. AddProperty respects the
	// EditCondition, so the bone fields keep appearing and disappearing with the
	// chosen Character.
	for (uint32 Index = 0; Index < HowMany; ++Index)
	{
		const TSharedPtr<IPropertyHandle> Child = Handle->GetChildHandle(Index);
		if (!Child.IsValid())
		{
			continue;
		}

		// Show is already in the header; repeating it here would be two
		// checkboxes for the same thing.
		if (Child->GetProperty() != nullptr && Child->GetProperty()->GetFName() == InTheHeader)
		{
			continue;
		}

		Builder.AddProperty(Child.ToSharedRef());
	}

	Builder.AddCustomRow(LOCTEXT("AttachmentAlignSearch", "Align to world"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("AttachmentAlignName", "Align"))
			.Font(Utils.GetRegularFont())
		]
		.ValueContent()
		.MinDesiredWidth(200.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text_Lambda([Handle]() { return FofuxoAttachmentUI::ButtonLabel(Handle); })
			.ToolTipText(LOCTEXT("AttachmentAlignTip",
				"Puts this row's bone with its axes on top of the world's axes, in the retarget pose of "
				"its own side -- the same as the toolbar's Align to world, only on the bone the row "
				"already names, with no need to select it in the viewport.\n\n"
				"On an attachment set to Both it aligns both sides at once, each on its own bone, in a "
				"single Ctrl+Z. That is the point: the two end up in the same orientation because of a "
				"reference external to both, and not a measurement taken from one of them.\n\n"
				"It works outside Editing Retarget Pose, but only in that mode can the result be seen."))
			.OnClicked_Lambda([Handle]() { return FofuxoAttachmentUI::Align(Handle); })
		];
}

#undef LOCTEXT_NAMESPACE
