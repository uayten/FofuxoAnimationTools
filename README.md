# Fofuxo's Animation Tools

An editor plugin for **Unreal Engine 5.8** that does two things: gets animation
*out* of Unreal in one piece, and adds to the **IK Retargeter** the tools it
doesn't have.

**Export**

- Many Animation Sequences **plus** a Skeletal Mesh into **one file**, each
  animation kept as a **take named after the asset** — instead of thirty files
  full of `Take 001`.
- **FBX, USD and glTF** from the same dialog.
- **Presets per destination** — axis, unit and scale, set once. **Blender** and
  **Unity** come built in (Unity gets Y-up, which it does not convert on import);
  add your own for Maya, Godot, or whatever you feed.
- Splits into numbered files past N takes, for importers that choke on a big FBX.

**IK Retargeter**

- **Live Retarget** — rotate bones with the animation *running*, on the frame
  where the error actually shows. Fixing fingers around a sword stops being
  guesswork.
- **Click bones by proximity** — near is enough. No more aiming at a line one
  pixel wide.
- **Stick bones** — thin bone drawing at a constant size on screen, like
  Blender's stick mode.
- **Preview attachments stored in the retargeter** — the sword in the hand, and
  it survives reimporting the rig.
- **Bone offset that ships** — nudge the weapon bone and the nudge comes out in
  the exported animation.
- **Source viewport** — a second viewport locked to the matching source bone,
  following it frame by frame.
- **Mirror**, **Align**, **Alt+R to reset a bone**, and **retarget poses that
  travel** between projects and onto MetaHumans.

Free, MIT, and it stays that way. [Install](#install) ·
[What's here](#contents)

---

## Contents

- [About this project](#about-this-project)
- [Install](#install)
  - [Where the buttons are](#where-the-buttons-are)
  - [If the rebuild fails](#if-the-rebuild-fails)
- [Releases and Fab](#releases-and-fab)
- [Export](#export)
- [IK Retargeter](#ik-retargeter)
  - [Live Retarget](#live-retarget)
  - [Editable Transforms panel](#editable-transforms-panel)
  - [Alt+R](#altr)
  - [Align (Esticar)](#align-esticar)
  - [Mirror](#mirror)
  - [Copy pose](#copy-pose)
  - [Preview Attachments (Fofuxo)](#preview-attachments-fofuxo)
  - [Source viewport](#source-viewport)
  - [Stick bones, and being able to click them](#stick-bones-and-being-able-to-click-them)
  - [Export animations](#export-animations)
- [Building](#building)
- [Renaming the plugin](#renaming-the-plugin)
- [Known limits](#known-limits)
- [License](#license)
- [A note on the source](#a-note-on-the-source)

---

## About this project

It came out of a concrete pipeline — character rigged in Blender, retargeted in
Unreal, exported back out — and every button exists because some step of that
loop was costing half an hour a day.

I'm a 3D artist, not a programmer. **Every line of C++ in here was written by
directing an AI** — vibe coding, if you like the term. What I brought was the
problem, the design calls, and the testing: I'm the one who sat in the IK
Retargeter and found out that a bone hit proxy is one pixel wide, that the
retarget pose can't store per-bone translation, and that hiding the engine's
skeleton also deletes the thing you click on.

I think that's a real way to build engine tooling, and **I'd love to do it at
Epic** — working on the engine the way this plugin was made, with someone who
knows the pipeline pain sitting where the decisions get made.

And honestly, **the best outcome for this plugin is not existing.** Every feature
here works around something the IK Retargeter almost does: a pose editor that
can't run while the animation does, a bone you can't click, an attachment that
dies with a reimport. If Epic built these natively — their way, better than mine,
with access to the parts of `IKRigEditor` that aren't exported — I would close
this repository happily. Until then it's here, and it's MIT.

**Anyone is welcome to try this retargeting workflow, free, forever.** That part
is deliberate: [Kawaii Physics](https://github.com/pafuhana1213/KawaiiPhysics) is
the model — a tool everyone can simply have. If the Live Retarget idea or the
proximity bone picking is useful to you, take it, break it, tell me what went
wrong. Issues and forks are very welcome.

— [Antônio Froz](https://github.com/uayten)

---

## Install

**Unreal Engine 5.8.** The GitHub version is tested against the newest engine
only — see [Releases and Fab](#releases-and-fab).

**1. Get the files.** Either `git clone` the repository, or use the green
**Code → Download ZIP** button and unzip it.

**2. Put it in your project.** You want this:

```
YourProject/
  YourProject.uproject
  Plugins/
    FofuxoAnimationTools/
      FofuxoAnimationTools.uplugin
      Source/
      Resources/
```

Create the `Plugins` folder if the project doesn't have one. The folder name
doesn't matter — Unreal looks for the `.uplugin` file, not for a particular
folder name. What does matter is that the `.uplugin` sits *directly* inside that
folder and not one level deeper: a ZIP download unzips into a
`FofuxoAnimationTools-main/` wrapper, so it's easy to end up with one folder too
many.

**3. Open the `.uproject`.** Unreal will say the modules are missing and offer to
rebuild them. Say **Yes**. The first build takes a few minutes.

**4. That's it.** A plugin sitting in your project's `Plugins` folder is enabled
automatically — you don't have to tick anything. **IK Rig**, **USD Importer** and
**glTF Exporter** are required and get enabled along with it; all three ship with
the engine.

To check that it's in: **Edit → Plugins**, search for *Fofuxo*.

### Where the buttons are

- **Export**: right-click one or more Animation Sequences in the Content
  Browser → `Fofuxo -- Exportar`.
- **Retarget tools**: open any **IK Retargeter** asset. There is a **Fofuxo**
  section on the toolbar, and an **Anexos de Preview (Fofuxo)** op you can add
  from the Op Stack.

### If the rebuild fails

You need **Visual Studio** with the *Game development with C++* workload — the
version Epic lists for your engine release. Unreal cannot compile a C++ plugin
without it, and what it tells you is not helpful:

> *Missing Modules — the following modules are missing or built with a different
> engine version*, followed by *could not be compiled. Try rebuilding from source
> manually.*

To see the real error, build from a terminal:

```
"<Engine>/Build/BatchFiles/Build.bat" <Project>Editor Win64 Development -Project="<full path to>/<Project>.uproject" -WaitMutex
```

Two things trip people up here, and neither one is a broken build:

- **Quote the project path.** If it contains a space — and `Unreal Projects` does
  — an unquoted path arrives truncated, and the tool reports missing modules for
  a project it never found.
- **Deleting `Intermediate/` and `Binaries/` is almost never the fix.** Compile
  first and read the error.

If your project is Blueprint-only and the rebuild still refuses, add any empty
C++ class (**Tools → New C++ Class**). That turns it into a C++ project, and the
plugin builds along with it.

## Releases and Fab

The plan here is the one [DlgSystem](https://github.com/NotYetGames/DlgSystem) by
NotYetGames uses, because it's the honest one for a solo project:

| where | what you get |
|---|---|
| **GitHub** | the latest source, always. Tested against the newest engine version only — right now 5.8. Free, MIT. |
| **Releases** (here) | tagged snapshots. Source, same as above, but pinned: you know which commit you have and what changed. |
| **Fab** (planned) | ready-to-use builds, one per engine version, cut less often and from a commit that sat still for a while. Also free. |

The difference that matters to you: **from GitHub you compile it yourself**, which
needs Visual Studio. A Fab build is prebuilt, so it drops into a Blueprint-only
project with nothing installed. That's the whole reason the Fab half of this plan
exists, and until it does exist, the install steps above are the way in.

Nothing here is or will be paid.

---

## Export

Unreal's FBX exporter writes one animation per file and doesn't name the takes.
Whoever receives them on the other side — Blender, Maya, Unity — opens thirty
files and sees thirty takes called `Take 001`.

**Right-click Animation Sequences in the Content Browser → `Fofuxo -- Exportar`.**

- Every selected animation plus a Skeletal Mesh in a single file, **each
  animation as a take named after the asset**.
- **FBX, USD and glTF**, from the same dialog.
- **Targets**: saved presets of up axis, forward axis, unit and scale — set once,
  then never think about it again.
- **Animations per file**: past a limit, the export splits into numbered files.
  Some importers choke on an FBX with hundreds of takes.
- With no animation selected, you get just the mesh.

Two targets ship with the plugin, and you can add your own:

| target | up axis | unit | why |
|---|---|---|---|
| **Blender** | Z | centimetres | The file comes out identical to Unreal's native export, and the importer decides the object scale. Metres looked like the fix for "object imports at 0.01" and isn't: the FBX SDK's `ConvertScene` doesn't touch vertices, it puts the scale on the nodes, so the 0.01 shows up anyway — just from inside the file. |
| **Unity** | Y | centimetres | Unity converts the unit on import by itself, but **not** the axis. Handing it Y-up is the whole difference between a character standing up and one lying on its face. |

glTF is always metres and Y-up, so the target's unit and up axis have nothing to
choose there — but the up axis still decides something. Unreal's FBX, once it has
been turned Y-up for Unity, ends up at `(-X, Z, -Y)` of the Unreal position;
Unreal's own glTF exporter writes `(X, Z, Y)`. Same scene, two bases half a turn
apart around the up axis, and each exporter bakes its own basis into every single
bone — so a character extracted from one file will not take an animation
extracted from the other, and nothing warns you: the body just folds into
impossible shapes. On the **Unity** target the glTF is turned to match the FBX,
and the skeleton is parented next to the mesh instead of inside it, the way FBX
does it — Unity clips address bones by path. On **Blender** the file is written
exactly as Unreal's exporter writes it, which is what Blender's importer expects.

There's a console command too, for scripts and builds:

```
Fofuxo.Export <output.fbx> <mesh path> [animation folder] [Unity]
```

The trailing `Unity` switches that run to the Unity target.

---

## IK Retargeter

Everything below shows up in a **Fofuxo** section on the IK Retargeter toolbar.

### Live Retarget

The hand problem. You edit the retarget pose while looking at the *ref pose*, and
in the ref pose the hand is open — you cannot see whether the fingers close
around the sword, which is the only thing that matters about fingers. You find
out they're wrong when the animation plays, and by then the pose editor is off.

With Live Retarget on, the gizmo appears **in Running Retarget**: pause the
animation on whichever frame you want, click a target bone, and rotate it.

What the gizmo writes is still the retarget pose, not a per-frame fix — the
retargeter has nowhere to store per-frame corrections. But the math makes that
worth it. In an FK chain a bone's output is

```
Output(B) = SourceDelta(B) · RetargetPose(B)
```

Post-multiplying an `X` into the retarget pose post-multiplies the same `X` into
the output, **on every frame**. So rotating the finger while looking at frame 37
writes the offset that produces exactly that rotation on frame 37 — and the same
rotation, in world space, on all the others. For fingers that's correct: the
error of a finger gripping a sword is constant, and the frame is only there so
you can see it.

Target side only. The source animation is the retarget's input; there is nothing
to adjust there.

### Editable Transforms panel

The engine's bone details panel, unlocked: in Live Retarget you can **type** the
rotation instead of only dragging the gizmo. They're two sides of the same write,
and there was no reason for one of them to be greyed out.

### Alt+R

Resets the selected bones' rotation in the retarget pose. Works in both modes —
including with the animation running, where the engine's *Reset Selected Bones*
refuses to execute. It's a registered command, so it shows up under **Edit →
Editor Preferences → Keyboard Shortcuts** and the key can be rebound.

### Align (Esticar)

Straightens bones while you build the retarget pose. Four modes, in the dropdown
next to the button:

| mode | what it does |
|---|---|
| Selected | aligns each bone with its parent's rotation — straightens the finger |
| With children | the same, walking down the chain |
| To the last | aligns the whole chain by its tip |
| To world | points the bone's tip down a world axis |

**To world** is the one that makes a weapon and a hand match across two different
skeletons: the reference is external to both, so they agree without anyone
measuring anything.

### Mirror

Repeats on the opposite bone the rotation you just gave one — rotate `thigh_l`
and `thigh_r` follows, mirrored. It finds the pair by name (`l`/`r`,
`left`/`right`, `lt`/`rt`, separated by `_`, `.`, `-`, space, or glued in
camelCase). Bones with no pair stay out.

If you move both sides at once — both selected in the gizmo, a global Auto Align,
a Ctrl+Z — the mirror stays out of it.

### Copy pose

Brings the retarget pose **from another retargeter** into the side you're
editing, matching bones by name. This is for the fix that doesn't travel: if
every retarget in the project starts from the same character, all of them share
the same source-side pose, and fixing one doesn't fix the others.

There's also **pose as an asset**, which crosses projects: save the Manny's pose
into an asset and apply it in another project, or on a MetaHuman.

### Preview Attachments (Fofuxo)

A retarget op, on the stack. It hangs a mesh off a bone — the sword in the hand —
for the viewport only.

This is not the skeleton editor's *Add Preview Asset*: that one lives on the
`USkeletalMesh` and the `USkeleton`, and disappears when the rig is reimported as
a new asset. This one lives in the retargeter, which is the asset that survives
swapping both characters.

Each row has:

- **Character** — source, target, or both (the same weapon on both sides, to
  compare).
- **Source bone / Target bone** — two fields, because the two skeletons almost
  never call the same bone by the same name.
- **Offset the bone** — moves the target bone, and **it comes out in exported
  animations**. This is what fixes the weapon not being in the hand. With Live
  Retarget on you can drag it with the translate gizmo.
- **Preview fit** — moves only the attached mesh; dies in the viewport. For a
  crooked model pivot, and for the source side, which the offset can't reach.
- **Align to world** — the same alignment as the Align button, inside the op.

> **This op has to sit after FK Chains and Run IK Rig on the stack.** Ops run in
> order and the last writer wins: with it on top, FK Chains recomputes the bone
> afterwards and the offset silently disappears.

### Source viewport

**Window → Fonte (Fofuxo)**, or the toolbar button. A second viewport onto the
*same* scene, with the camera locked to the source bone that corresponds to the
selected bone — and following it frame by frame while the animation runs.

In one viewport you rotate the target's finger; in the other you see where the
reference finger is on that same frame, without flying the camera between the two
characters.

The correspondence comes from the chain mapping: find the target chain containing
the bone, then take the bone at the same proportional position in the mapped
chain.

### Stick bones, and being able to click them

Two different things with the same root cause: in a hand, Unreal's bones are
small, numerous and nearly impossible to hit.

**Clicking near a bone is now enough** — always, no toggle. Unreal selects by hit
proxy and reads *one* pixel, the one under the cursor. Here a 22-pixel box around
the cursor is read instead, and the bone proxy closest to the centre wins. The
proxy that's found is handed back to the engine's own mode, which does the
selection its way — details panel, hierarchy and gizmo all update by themselves,
with no parallel code path.

**Stick bones** (toolbar toggle) shrinks the engine's octahedral bone and draws a
thin line between joints with a circle on each, **at a constant size on screen**:
the engine's bone is measured in world units, so the same drawing that is a ball
at the wrist vanishes when the camera pulls back.

It shrinks rather than hides, and that's deliberate: **the bone's identity for
clicking lives in the engine's drawing.** Hiding it would delete the ability to
select. The shrunk size is the retargeter's `BoneDrawSize`, the same value as the
slider under **Character → Bones**; turning the toggle off restores it.

### Export animations

In the retarget editor's Asset Browser:

- **Export Selected Animations** — the engine's batch retarget, without leaving
  the editor.
- **Redo the already-exported ones** — re-exports whatever was exported before,
  to the same targets. After touching the retarget pose, it's one click.

---

## Building

The editor must be **closed**: Live Coding locks the whole build while it's open,
and doesn't even show you the compile error.

```
"<Engine>/Build/BatchFiles/Build.bat" <Project>Editor Win64 Development -Project="<path>/<Project>.uproject" -WaitMutex
```

When Unreal only says *"could not be compiled. Try rebuilding from source
manually"*, that line is what shows the real error. Deleting `Intermediate/` and
`Binaries/` is almost never the fix — compile first and read the error.

## Renaming the plugin

Two places, and only two:

1. `FriendlyName` in `FofuxoAnimationTools.uplugin`.
2. `FOFUXO_NAME` and `FOFUXO_SHORT_NAME` in
   [`Source/FofuxoCommon/FofuxoName.h`](Source/FofuxoCommon/FofuxoName.h), where
   every piece of UI text comes from.

Folder and module names are plumbing — they show up in file paths and
`IMPLEMENT_MODULE`, and changing them is work that changes nothing you can see.

## Known limits

- **The Source viewport tab doesn't come back on its own** when the retarget
  editor reopens: it registers half a second after the editor opens, and the
  saved layout was restored before that. Reopen it from Window.
- **Saving the RTG with stick bones on stores the shrunk `BoneDrawSize`.**
  Turning the toggle off restores the value and the next save fixes it.
- **Ctrl+Z in Live Retarget sometimes drops the mode** back to Editing Retarget
  Pose. The retargeter's `PostUndo` rebuilds the preview meshes and touches
  playback, and every setter needed to fix that from the inside is unexported
  from `IKRigEditor`. There's a conditional workaround that re-runs the toolbar
  command when the mode falls back on its own.

## License

[MIT](LICENSE). Use it, ship it, sell what you make with it, fork it and rename
it. The only thing the licence asks is that the copyright notice travels with
copies of the plugin's own source.

## A note on the source

The code, the comments and the in-editor text are in **English**. They were in
Portuguese until recently — it started as a personal tool and I saw no reason to
write comments in a language I think worse in — and the translation is now done,
identifiers included, in one pass.

If you have a retargeter or a pose asset saved by an older build, it still opens:
`Config/DefaultEngine.ini` carries the redirects from the old struct, enum, class
and property names to the new ones. Two things do not survive, and both are
preferences rather than data: the export targets you had created in the window,
and the toolbar's remembered toggles. Set them once more and they stay.
