// Fofuxo -- the bone's Transforms panel, editable in Live Retarget

#include "FofuxoBoneDetails.h"

#include "FofuxoLiveRetarget.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "RetargetEditor/IKRetargetDetails.h"
#include "RetargetEditor/IKRetargetEditorController.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "SAdvancedTransformInputBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "FofuxoRetargetProps"

namespace FofuxoBoneDetails
{
	/**
	 * Whether writing into the retarget pose applies right now.
	 *
	 * The engine answers `GetRetargeterMode() == EditRetargetPose` and stops
	 * there. Here Live Retarget comes in through the same door: in Running
	 * Retarget the write is the same, only you see the result in the animation
	 * instead of in the ref pose.
	 *
	 * Target only, for the same reason as the gizmo: the source's animation is the
	 * retarget's input, and there is nothing to adjust in it.
	 */
	static bool CanEdit(TWeakPtr<FIKRetargetEditorController> Weak)
	{
		const TSharedPtr<FIKRetargetEditorController> Who = Weak.Pin();
		if (!Who.IsValid())
		{
			return false;
		}

		const ERetargeterOutputMode Mode = Who->GetRetargeterMode();

		if (Mode == ERetargeterOutputMode::EditRetargetPose)
		{
			return true;
		}

		return FFofuxoLiveRetarget::IsOn()
			&& Mode == ERetargeterOutputMode::RunRetarget
			&& Who->GetSourceOrTarget() == ERetargetSourceOrTarget::Target;
	}
}

void FFofuxoBoneDetails::Register()
{
	// The engine's has to be in place already: registering the same class replaces
	// the previous one, and if IKRigEditor came up after us it would undo this. A
	// LoadModule of an already-loaded module does nothing.
	FModuleManager::Get().LoadModule(TEXT("IKRigEditor"));

	FPropertyEditorModule& Properties =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	Properties.RegisterCustomClassLayout(
		UIKRetargetBoneDetails::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FFofuxoBoneDetails::MakeInstance));
}

void FFofuxoBoneDetails::Forget()
{
	if (!FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		return;
	}

	FPropertyEditorModule& Properties =
		FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	// Giving the engine's back would be ideal, and it can't be done: its
	// MakeInstance is inline in the header, but constructing the class needs the
	// vtable, and the virtuals live in IKRigEditor's .cpp with no export. So only
	// our registration goes. The bone panel is left with no customization at all
	// -- only the bone's name, without the transform rows -- until this module is
	// next loaded. That is visible in a Live Coding pass, and at no other moment.
	Properties.UnregisterCustomClassLayout(UIKRetargetBoneDetails::StaticClass()->GetFName());
}

TSharedRef<IDetailCustomization> FFofuxoBoneDetails::MakeInstance()
{
	return MakeShareable(new FFofuxoBoneDetails);
}

void FFofuxoBoneDetails::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(Bones);
}

FString FFofuxoBoneDetails::GetReferencerName() const
{
	return TEXT("FFofuxoBoneDetails");
}

void FFofuxoBoneDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	Bones.Reset();

	for (const TWeakObjectPtr<UObject>& Object : DetailBuilder.GetSelectedObjects())
	{
		if (UIKRetargetBoneDetails* Bone = Cast<UIKRetargetBoneDetails>(Object.Get()))
		{
			Bones.Add(Bone);
		}
	}

	if (Bones.IsEmpty() || !Bones[0]->EditorController.IsValid())
	{
		return;
	}

	const TWeakPtr<FIKRetargetEditorController> Weak = Bones[0]->EditorController;
	const TSharedPtr<FIKRetargetEditorController> Who = Weak.Pin();

	if (Who->AssetController == nullptr)
	{
		return;
	}

	const ERetargetSourceOrTarget Side = Who->GetSourceOrTarget();
	const bool bPelvis = Bones[0]->SelectedBone == Who->AssetController->GetPelvisBone(Side);

	// This only decides which row starts open. All four are built anyway, and the
	// button switches between them without rebuilding anything.
	const bool bCanRightNow = FofuxoBoneDetails::CanEdit(Weak);

	const TArray<EIKRetargetTransformType> Types =
	{
		EIKRetargetTransformType::RelativeOffset,
		EIKRetargetTransformType::Bone,
		EIKRetargetTransformType::Current,
		EIKRetargetTransformType::Reference,
	};

	const TArray<FText> Labels =
	{
		LOCTEXT("RowOffset", "Relative Offset"),
		LOCTEXT("RowBone", "Bone"),
		LOCTEXT("RowCurrent", "Current"),
		LOCTEXT("RowReference", "Reference"),
	};

	const TArray<FText> Tips =
	{
		LOCTEXT("RowOffsetTip",
			"How far the retarget pose moves this bone from the ref pose. It is what the gizmo "
			"writes, and what Alt+R clears."),
		LOCTEXT("RowBoneTip",
			"Where the retarget pose puts this bone, relative to its parent -- the ref pose with "
			"the offset above already added."),
		LOCTEXT("RowCurrentTip",
			"Where the bone is right now in the viewport. With an animation running this changes "
			"every frame; it is reading, never writing."),
		LOCTEXT("RowReferenceTip", "Where the bone sits in the mesh's ref pose. Reading."),
	};

	const TArray<EIKRetargetTransformType> Open =
	{
		bCanRightNow ? EIKRetargetTransformType::RelativeOffset : EIKRetargetTransformType::Current
	};

	const TSharedPtr<SSegmentedControl<EIKRetargetTransformType>> Choice =
		SSegmentedControl<EIKRetargetTransformType>::Create(
			Types,
			Labels,
			Tips,
			TAttribute<TArray<EIKRetargetTransformType>>(Open));

	DetailBuilder.EditCategory(TEXT("Selection")).SetSortOrder(1);

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Transforms"));
	Category.SetSortOrder(2);

	Category.AddCustomRow(FText::FromString(TEXT("TransformType")))
	.ValueContent()
	.MinDesiredWidth(375.f)
	.MaxDesiredWidth(375.f)
	.HAlign(HAlign_Left)
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			Choice.ToSharedRef()
		]
	];

	// Weak pointers, and a copy of them inside each lambda.
	//
	// What used to be here was what the engine does: a TArrayView over this
	// customization's `Bones`. The view is a raw pointer into the object, and the
	// fields' delegates take it by copy -- so it needs the customization to outlive
	// the widgets. It doesn't: **Ctrl+Z rebuilds the bone panel**, the old
	// customization dies and the array goes with it, but its widgets still exist
	// for one frame, and a rotation field losing focus *commits* the value on that
	// frame. The delegate read the list through the dead view and the editor fell
	// over right there.
	//
	// In the engine the same pattern doesn't show up because there the writing
	// fields only exist in Editing Retarget Pose, where no field has focus at undo
	// time. It is Live Retarget that leaves them enabled in Running Retarget -- so
	// the fix belongs here.
	TArray<TWeakObjectPtr<UIKRetargetBoneDetails>> Weaks;
	Weaks.Reserve(Bones.Num());

	for (const TObjectPtr<UIKRetargetBoneDetails>& Bone : Bones)
	{
		Weaks.Add(Bone);
	}

	for (int32 Which = 0; Which < Types.Num(); ++Which)
	{
		const EIKRetargetTransformType Type = Types[Which];

		const bool bWrites = Type == EIKRetargetTransformType::RelativeOffset
			|| Type == EIKRetargetTransformType::Bone;

		SAdvancedTransformInputBox<FTransform>::FArguments Args =
			SAdvancedTransformInputBox<FTransform>::FArguments()
			// On the reading rows the whole transform makes sense. On the writing
			// ones it doesn't: whoever writes location writes the root offset, which
			// is one single value for the whole pose, and scale the retarget pose
			// doesn't store.
			.ConstructLocation(!bWrites || bPelvis)
			.ConstructRotation(true)
			.ConstructScale(!bWrites)
			.DisplayRelativeWorld(true)
			.DisplayScaleLock(false)
			.AllowEditRotationRepresentation(true)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.UseQuaternionForRotation(true);

		if (bWrites)
		{
			// An attribute, not a value: turning Live Retarget on in the toolbar
			// rebuilds no panel, so a bool decided here would stay wrong until you
			// clicked another bone.
			Args.IsEnabled(TAttribute<bool>::CreateLambda(
				[Weak]() { return FofuxoBoneDetails::CanEdit(Weak); }));

			// The engine's OnMultiNumericValueCommitted takes the list by
			// TArrayView, so it stays out: the one iterating here is us, by weak
			// pointer. What it calls on each bone -- OnNumericValueCommitted -- is
			// public and exported, and it is the same write.
			Args.OnNumericValueCommitted_Lambda([Weaks, Type](
				ESlateTransformComponent::Type Part,
				ESlateRotationRepresentation::Type Representation,
				ESlateTransformSubComponent::Type Sub,
				FVector::FReal Value,
				ETextCommit::Type Commit)
			{
				for (const TWeakObjectPtr<UIKRetargetBoneDetails>& One : Weaks)
				{
					if (UIKRetargetBoneDetails* Bone = One.Get())
					{
						Bone->OnNumericValueCommitted(
							Part, Representation, Sub, Value, Commit, Type, /*bIsCommit*/ true);
					}
				}
			});

			Args.OnNumericValueChanged_Lambda([Weaks, Type](
				ESlateTransformComponent::Type Part,
				ESlateRotationRepresentation::Type Representation,
				ESlateTransformSubComponent::Type Sub,
				FVector::FReal Value)
			{
				for (const TWeakObjectPtr<UIKRetargetBoneDetails>& One : Weaks)
				{
					if (UIKRetargetBoneDetails* Bone = One.Get())
					{
						Bone->OnNumericValueCommitted(
							Part, Representation, Sub, Value, ETextCommit::Default, Type,
							/*bIsCommit*/ false);
					}
				}
			});

			Args.OnBeginSliderMovement_Lambda([](
				ESlateTransformComponent::Type,
				ESlateRotationRepresentation::Type,
				ESlateTransformSubComponent::Type)
			{
				GEditor->BeginTransaction(LOCTEXT("EditFromPanel", "Edit Retarget Pose Transform Slider"));
			});

			Args.OnEndSliderMovement_Lambda([](
				ESlateTransformComponent::Type,
				ESlateRotationRepresentation::Type,
				ESlateTransformSubComponent::Type,
				double)
			{
				GEditor->EndTransaction();
			});
		}
		else
		{
			Args.IsEnabled(false);
		}

		Args.OnGetIsComponentRelative_Lambda([Weaks, Type](ESlateTransformComponent::Type Part)
		{
			return Weaks.ContainsByPredicate([&](const TWeakObjectPtr<UIKRetargetBoneDetails>& One)
			{
				const UIKRetargetBoneDetails* Bone = One.Get();
				return Bone != nullptr && Bone->IsComponentRelative(Part, Type);
			});
		});

		Args.OnIsComponentRelativeChanged_Lambda(
			[Weaks, Type](ESlateTransformComponent::Type Part, bool bRelative)
		{
			for (const TWeakObjectPtr<UIKRetargetBoneDetails>& One : Weaks)
			{
				if (UIKRetargetBoneDetails* Bone = One.Get())
				{
					Bone->OnComponentRelativeChanged(Part, bRelative, Type);
				}
			}
		});

		Args.OnGetNumericValue_Lambda([Weaks, Type](
			ESlateTransformComponent::Type Part,
			ESlateRotationRepresentation::Type Representation,
			ESlateTransformSubComponent::Type Sub) -> TOptional<FVector::FReal>
		{
			UIKRetargetBoneDetails* Owner = Weaks.IsEmpty() ? nullptr : Weaks[0].Get();
			if (Owner == nullptr)
			{
				return TOptional<FVector::FReal>();
			}

			TOptional<FVector::FReal> First = Owner->GetNumericValue(Type, Part, Representation, Sub);

			if (First)
			{
				for (int32 Other = 1; Other < Weaks.Num(); ++Other)
				{
					UIKRetargetBoneDetails* More = Weaks[Other].Get();
					if (More == nullptr)
					{
						continue;
					}

					const TOptional<FVector::FReal> Value =
						More->GetNumericValue(Type, Part, Representation, Sub);

					if (Value.IsSet())
					{
						// The engine's own slack: without it the floating-point noise
						// of the rotation sums makes the panel say "Multiple Values"
						// for two bones that are in the same place.
						constexpr double Precision = 1.e-2;
						if (!FMath::IsNearlyEqual(First.GetValue(), Value.GetValue(), Precision))
						{
							return TOptional<FVector::FReal>();
						}
					}
				}
			}

			return First;
		});

		Args.OnCopyToClipboard_UObject(Bones[0].Get(), &UIKRetargetBoneDetails::OnCopyToClipboard, Type);
		Args.OnPasteFromClipboard_UObject(Bones[0].Get(), &UIKRetargetBoneDetails::OnPasteFromClipboard, Type);

		Args.Visibility_Lambda([Choice, Type]() -> EVisibility
		{
			return Choice->HasValue(Type) ? EVisibility::Visible : EVisibility::Collapsed;
		});

		SAdvancedTransformInputBox<FTransform>::ConstructGroupedTransformRows(
			Category,
			Labels[Which],
			Tips[Which],
			Args);
	}
}

#undef LOCTEXT_NAMESPACE
