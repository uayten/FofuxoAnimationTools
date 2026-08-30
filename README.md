# Fofuxo's Animation Tools

An editor plugin for **Unreal Engine 5.8**. Two things that are really one: getting
animation *out* of Unreal, and making IK retargeting stop hurting.

It came out of a concrete pipeline — character rigged in Blender, retargeted in
Unreal, exported back out — and every button exists because some step of that
loop was costing half an hour a day.

---

## About this project

I'm a 3D artist, not a programmer. **Every line of C++ in here was written by
directing an AI** — vibe coding, if you like the term. What I brought was the
problem, the design calls, and the testing: I'm the one who sat in the IK
Retargeter and found out that a bone hit proxy is one pixel wide, that the
retarget pose can't store per-bone translation, and that hiding the engine's
skeleton also deletes the thing you click on.

I think that's a real way to build engine tooling, and **I'd love to do it at
Epic** — working on the engine the way this plugin was made, with someone who
knows the pipeline pain sitting where the decisions get made.

**Anyone is welcome to try this retargeting workflow.** If the Live Retarget idea
or the proximity bone picking is useful to you, take it, break it, tell me what
went wrong. Issues and forks are very welcome.

— [Antônio Froz](https://github.com/uayten)

---

## Install

Copy the folder into `YourProject/Plugins/` and open the project. Unreal compiles
it on first open.

Requires the **IK Rig** plugin (ships with the engine) and, for the scene
formats, **USD Importer** and **glTF Exporter** — all declared in the
`.uplugin`.

---

## Export

Unreal's FBX exporter writes one animation per file and doesn't name the takes.
Whoever receives them on the other side — Blender, Maya, Unity — opens thirty
files and sees thirty takes called `Take 001`.

**Right-click Animation Sequences in the Content Browser → `Fofuxo -- Exportar`.**

- Every selected animation plus a Skeletal Mesh in a single file, **each
  animation as a take named after the asset**.
- **FBX, USD and glTF**, from the same dialog.
- **Targets**: saved presets of up axis, forward axis, unit and scale — one for
  Blender, one for Unity, and you never think about it again.
- **Animations per file**: past a limit, the export splits into numbered files.
  Some importers choke on an FBX with hundreds of takes.
- With no animation selected, you get just the mesh.

There's a console command too, for scripts and builds:

```
Fofuxo.Exportar <output.fbx> <mesh path> [animation folder] [Unity]
```

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

1. `FriendlyName` in `FofuxoExporter.uplugin`.
2. `FOFUXO_NOME` and `FOFUXO_NOME_CURTO` in
   [`Source/FofuxoComum/FofuxoNome.h`](Source/FofuxoComum/FofuxoNome.h), where
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

## A note on the source

The code, the comments and the in-editor text are in **Portuguese** — it started
as a personal tool and I never saw a reason to write comments in a language I
think worse in. The identifiers are Portuguese too (`FofuxoOssosNaTela` is "bones
on screen"). If that's a barrier for you and you want to use this, say so in an
issue; translating is mechanical.
