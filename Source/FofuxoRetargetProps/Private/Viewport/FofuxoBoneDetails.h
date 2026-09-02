// Fofuxo -- the bone's Transforms panel, editable in Live Retarget

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "UObject/GCObject.h"

class UIKRetargetBoneDetails;

/**
 * Replaces the "Transforms" panel of the bone selected in the retarget editor.
 *
 * The engine's locks the fields outside Editing Retarget Pose. The sum it makes
 * is a single line, in FIKRetargetBoneDetailCustomization:
 *
 *     bIsEditable = bIsEditingPose && (bIsRelativeOffset || bIsBoneOffset)
 *
 * -- and `bIsEditingPose` is `GetRetargeterMode() == EditRetargetPose`. With Live
 * Retarget on that is false, so the gizmo turns the bone in the viewport and the
 * panel next to it shows the result greyed out. They are two sides of the same
 * write, and there was no reason for one of them to be locked: the path that
 * writes (CommitValueAsRelativeOffset) doesn't consult the mode, it just writes
 * into the retarget pose -- exactly what the gizmo does.
 *
 * What changes here, and only this:
 *
 * - **All four rows always exist.** The engine builds either the reading pair
 *   (Current, Reference) or the editing trio (Relative Offset, Bone, Reference),
 *   according to the mode *at the instant the panel is built*. Since turning Live
 *   Retarget on rebuilds no panel, a choice made at that instant would stay wrong
 *   until you clicked another bone. With all four always built, the button you
 *   want is always there.
 * - **"Enabled" is an attribute, not a value.** It is re-evaluated every frame,
 *   so turning Live Retarget on and off unlocks and locks the field right away.
 *
 * The rest is the engine's panel: the same rows, the same world/local buttons,
 * the same copy and paste, and the writing goes through the same
 * UIKRetargetBoneDetails methods, which are exported.
 *
 * **Location only shows up for editing on the pelvis**, as in the engine, and for
 * the same reason: whoever writes location writes SetRootTranslationDelta, which
 * is one single value for the whole pose. Leaving the field open on a finger
 * would be offering a control that moves another bone.
 */
class FFofuxoBoneDetails : public IDetailCustomization, public FGCObject
{
public:

	/**
	 * Puts this customization in place of the engine's.
	 *
	 * Registering the same class again replaces the previous one in the
	 * PropertyEditor's map -- and that is why Forget() gives the engine's back
	 * rather than merely unregistering: without it, a Live Coding pass would leave
	 * the bone panel bare.
	 */
	static void Register();
	static void Forget();

	static TSharedRef<IDetailCustomization> MakeInstance();

	// IDetailCustomization
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	// End IDetailCustomization

	// FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
	// End FGCObject

private:

	TArray<TObjectPtr<UIKRetargetBoneDetails>> Bones;
};
