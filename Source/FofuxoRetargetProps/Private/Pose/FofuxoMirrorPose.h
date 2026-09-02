// Fofuxo -- mirroring the retarget pose

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class FIKRetargetEditor;
class USkeletalMesh;

/**
 * Repeats on the bone of the other side the rotation you have just given a bone,
 * in Editing Retarget Pose.
 *
 * The retarget editor doesn't have this: rotating "thigh_l" to fix the pose
 * leaves "thigh_r" where it was, and the only way out is doing the sum in your
 * head and typing the opposite into Details. With the mirror on, the other side
 * follows.
 *
 * There is no "the pose changed" event to listen to -- the edit mode writes
 * straight into the asset through UIKRetargeterController. So this is a watcher:
 * it keeps a copy of the offset map and, every frame, compares. A bone that
 * changed and has a partner gets mirrored; a bone whose partner changed on the
 * same frame stays put, which is the case of you having moved both on purpose
 * (both selected in the gizmo, a global Auto Align, a Ctrl+Z).
 *
 * The writes land inside the transaction the gizmo has already opened, so one
 * Ctrl+Z undoes both sides at once.
 */
class FFofuxoMirrorPose
{
public:

	/** Starts the watcher. The button's state comes from the ini. */
	void Start();

	/**
	 * Stops it and forgets everything.
	 *
	 * Mandatory at module shutdown: the ticker calls code from this DLL, and a
	 * Live Coding pass with it still registered takes the editor down.
	 */
	void Stop();

	bool IsOn() const { return bOn; }
	void Toggle();

	/**
	 * Puts this editor on the watcher's list, if it isn't there already.
	 *
	 * It comes from the module's slow tick, which already walks the open editors
	 * because of the attachments -- discovering editors again here would be
	 * repeating that walk.
	 */
	void Follow(FIKRetargetEditor& Editor);

	/**
	 * The names that could be this one's other side -- just the names, without
	 * checking whether the bone exists.
	 *
	 * It recognizes the side as a segment separated by "_", ".", "-" or a space
	 * (thigh_l, arm.L, L-Hand, "Bip01 L UpperArm"), in any case, written l/r,
	 * left/right or lt/rt; and also the letter glued on in camelCase (HandL,
	 * LHand). One name can give more than one candidate -- "L_arm_l" gives two --
	 * and the skeleton is what picks.
	 */
	static void MirroredNames(const FString& Name, TArray<FString>& OutCandidates);

private:

	/** What the watcher knows about one open retarget editor. */
	struct FWatched
	{
		TWeakPtr<class FAssetEditorToolkit> Toolkit;

		// Where the cache was born from: change any of these and the cache is
		// rebuilt.
		TWeakObjectPtr<USkeletalMesh> Mesh;
		uint8 Side = 0;
		FName Pose;

		// The offset map as it stood last frame.
		TMap<FName, FQuat> Snapshot;

		// Ref pose rotations in component space, by bone index.
		TArray<FQuat> RefComponent;

		// Bone -> bone of the other side. With no partner it goes in as NAME_None,
		// so as not to search again every frame.
		TMap<FName, FName> Partners;

		// 0 = X, 1 = Y, 2 = Z: the mirror plane's normal, deduced from the
		// skeleton itself. An Unreal character faces +X, so it is almost always Y.
		int32 Axis = 1;

		bool bHasCache = false;
	};

	bool Tick(float);
	void Check(FWatched& Watched, FIKRetargetEditor& Editor);
	static void RebuildCache(FWatched& Watched, USkeletalMesh* Mesh);

	TArray<FWatched> Watched;
	FTSTicker::FDelegateHandle Ticker;
	bool bOn = false;
};
