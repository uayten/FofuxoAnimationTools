# Fofuxo's Animation Tools

An Unreal editor plugin, the user's own, **public** repository
(`uayten/FofuxoAnimationTools`). It is not a submodule of BossRush: a change here
does not show up in BossRush's `git status`.

**Two products in one plugin**, and they do not talk to each other:

1. **The exporter** — several Animation Sequences and a Skeletal Mesh in one
   file, each animation named. FBX, USD and glTF.
2. **The IK Retarget tools** — preview attachments, adjusting with the animation
   running, a portable pose, a second viewport, bone selection by proximity.

Everything is in **English**: code, comments, UI text and README. It was all
Portuguese until the translation pass; if you find a Portuguese identifier, it
was missed.

## The modules

| module | type | what it is |
|---|---|---|
| `FofuxoExporter` | Editor | The window, the options, the targets, the console command, and the **FBX** writer. |
| `FofuxoUsdScene` | Editor | The **USD** writer, and nothing else. |
| `FofuxoGltfScene` | Editor | The **glTF** writer, and nothing else. |
| `FofuxoRetargetOps` | Runtime | `FFofuxoAttachmentsOp`, an op on the IK Retargeter's stack — it has to be Runtime because it lives inside the asset. |
| `FofuxoRetargetProps` | Editor | Everything else about the IK Retargeter: the toolbar, the modes, the panels. |

`Source/FofuxoCommon/` **is not a module** — it is an include folder holding
`FofuxoName.h`, so that no module depends on another one just for a string.

**Why USD and glTF are separate modules:** the engine's conversions take Pixar
and GLTFExporter types, which forces a link against those SDKs. Isolated, a
project without those engine plugins still loads the main one. They bind through
a delegate (`FofuxoUsdSceneWriter()` / `FofuxoGltfSceneWriter()`, declared in
`FofuxoExporter/Public/FofuxoSceneWriters.h`).

## Where things are

**One feature = one `FofuxoXxx.h/.cpp` pair.** Searching by name works, with no
exceptions. `Private/` is split by subject, and every subfolder is on the
module's include path — so an include is a bare file name and moving a file
between subfolders touches no other file.

```
FofuxoExporter/Private/
  Export/    the request, the options and targets, the batching, the scene delegates
  Writers/   FBX, and the one that dispatches USD and glTF
  UI/        the window, the flow from selection to file, the animation scan
  (root)     the module, the console command

FofuxoRetargetProps/Private/
  Attachments/  the manager (also the plugin's only editor walk), the details row
  Pose/         live retarget, align, mirror, copy, pose on disk, reset rotation
  Viewport/     source viewport, stick bones, bone details
  Export/       redo the already-exported ones
  (root)        the module, the toolbar, the commands
```

**There is one ticker walking the open retarget editors**, in
`Attachments/FofuxoAttachments.cpp`. Everything that needs an open editor rides
on it — the redo button, the Alt+R shortcut, the source viewport tab, the stick
bones, the mirror. Do not add a second walk.

## Where we touch engine privates

This is what breaks when the engine version goes up. First place to look when
something stops compiling after an update:

| where | what |
|---|---|
| `FofuxoExporter.Build.cs` | `PrivateIncludePaths` into `UnrealEd/Private`, for `FbxExporter.h`. |
| `FofuxoFbxWriter.cpp` | Explicit template instantiation (`TThief`) to call `ExportAnimSequenceToFbx`, `CorrectAnimTrackInterpolation` and read the `Scene` member, all private to `FFbxExporter`. |
| `FofuxoGltfScene.Build.cs` | `PrivateIncludePaths` into `GLTFExporter/Private`, for `GLTFMemoryArchive.h`. |
| `FofuxoGltfHalfTurn.cpp` | Inherits from `FGLTFContainerBuilder` to reach the protected `GetBufferData()`, and `const_cast`s `GetRoot()`. |
| `FofuxoRedoRetarget.cpp` | Walks the Slate tree to find `SIKRetargetAssetBrowser` and pushes a slot into its column; the tab id `AssetBrowser` is spelled out because the symbol does not link. |
| `FofuxoBonesOnScreen.cpp` | Recognizes the retargeter's bone proxy by the type's *name*, `HIKRetargetEditorBoneProxy`, because its `StaticGetType` is not exported. |
| `FofuxoBoneDetails.cpp` | Registers over `UIKRetargetBoneDetails`'s customization, and cannot give the engine's back. |

## Axis and unit, in all three formats

Each format settles this differently, and none of the three looks like the
others. One Unreal point ends up at:

| format | where it lands | who does it |
|---|---|---|
| FBX, Blender target | `(X, -Y, Z)`, Z up | the engine's mirror alone (`FFbxDataConverter`) |
| FBX, Unity target | `(-X, Z, -Y)`, Y up | that mirror **plus** `DeepConvertScene`, in `FofuxoFbxWriter.cpp` |
| glTF, Blender target | `(X, Z, Y)` | the engine's `FGLTFCoreUtilities` — static, no hook |
| glTF, Unity target | `(-X, Z, -Y)` | the above **plus** `ApplyHalfTurn` |
| USD | Unreal's, with `upAxis` in the metadata | `SetUsdStageUpAxis`; the engine converts on read |

The two glTF bases are the same one turned half a turn about the up axis, and
**each exporter bakes its basis into every bone** -- which is why turning the
root node fixes nothing. The whole reason is in the header of
[`FofuxoGltfHalfTurn.h`](Source/FofuxoGltfScene/Private/FofuxoGltfHalfTurn.h).

glTF's `ExportUniformScale` only multiplies lengths. It never touches a rotation.

## Names in saved assets

Struct, enum, class and property names are what Unreal writes into the asset
file. The translation pass renamed all of them, and
[`Config/DefaultEngine.ini`](Config/DefaultEngine.ini) carries the CoreRedirects
that keep old retargeters and old pose assets loading. **Rename one of those and
the redirect list needs the new line** — without it the attachment list silently
empties.

## Building

The editor must be **closed** -- Live Coding locks the whole build and doesn't
even show the error. Close it through NodeScribe's MCP (`save_all_and_quit`).

```powershell
& "E:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" BossRushEditor Win64 Development -Project="C:\Unreal Projects\BossRush\BossRush.uproject" -WaitMutex -NoUBA
```

`-NoUBA` is not optional on this machine: the Unreal Build Accelerator's DLL
injection is blocked and the build dies with error 740/9006. Deleting
`Intermediate/` and `Binaries/` is almost never the fix -- compile and read the
error. The exception is a folder or module rename, where the stale paths and DLL
names in there really do have to go.

## What cannot be tested from here

Compiling is not testing. **Exporting** needs the editor open and the window, and
the result can only be judged on the other side -- opening the file in Blender or
importing it into Unity. **The IK Retarget tools** need a retargeter open and a
hand on the viewport. In both cases: ask for the test, say what to look at, and
never describe as working what has only compiled.

## Working rules

- **Do not commit without asking.** The repository is public and usually has
  half-finished work in the tree.
- **One change per commit.** A file rename mixed with a behaviour change turns
  the diff into soup.
