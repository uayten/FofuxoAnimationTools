// Fofuxo -- the Align button inside the attachments op

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

/**
 * Puts an "Align to world" button on every row of the attachment list.
 *
 * The reason it exists is the path the weapon takes: you register the bone and
 * the mesh here, and then you need to align that bone. Without this, aligning
 * means leaving the panel, finding the bone in the hierarchy, selecting it,
 * checking that the mode armed in the toolbar is the right one, and clicking --
 * and the panel already knows the bone's name.
 *
 * The button does the same as the toolbar's Align to world, with two differences
 * that come for free from it being born here:
 *
 * 1. It depends on no selection, and so it works outside Editing Retarget Pose.
 *    The effect only *shows* in that mode, but the pose is written all the same.
 *
 * 2. On an attachment set to Both it aligns both sides in a single transaction,
 *    each with the bone the row names for that side. That is the case the
 *    toolbar cannot reach: there the selection is on one character at a time.
 *
 * The UIKRetargeter that owns the row is found by address: the list lives inside
 * the asset, so the open retargeter whose op contains *this* FFofuxoAttachment
 * is the owner. Comparing pointers avoids depending on the details panel's
 * plumbing, which involves UObject wrappers with no exported API.
 */
class FFofuxoAttachmentDetails : public IPropertyTypeCustomization
{
public:

	static TSharedRef<IPropertyTypeCustomization> Create();

	/** Turns the customization on and off. The module is the one that calls. */
	static void Register();
	static void Forget();

	// IPropertyTypeCustomization
	virtual void CustomizeHeader(
		TSharedRef<IPropertyHandle> Handle,
		FDetailWidgetRow& Row,
		IPropertyTypeCustomizationUtils& Utils) override;

	virtual void CustomizeChildren(
		TSharedRef<IPropertyHandle> Handle,
		IDetailChildrenBuilder& Builder,
		IPropertyTypeCustomizationUtils& Utils) override;
	// End IPropertyTypeCustomization
};
