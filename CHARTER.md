# GDMpv Charter

## Purpose

Build a Godot 4 GDExtension video player for VR-focused games that uses `libmpv` for playback and targets efficient, production-usable rendering, starting with Windows x86_64.

The intended use case is in-world video playback similar to VRChat / ChilloutVR media players, where video playback must leave enough GPU and CPU budget for the rest of the VR application.

## Problem Statement

Existing Godot + mpv experiments in this workspace are useful prototypes, but they do not provide the rendering path needed for a real VR title:

- `godot_mpv` renders with libmpv into an offscreen GL surface, then reads frames back to CPU with `glReadPixels()`, then uploads them into a new Godot texture each frame.
- `gdmpv` currently uses libmpv software rendering (`MPV_RENDER_API_TYPE_SW`) and uploads CPU frame buffers into a Godot `ImageTexture`.

Both approaches are too expensive for the target use case. For VR, the design goal is a GPU-native path with no per-frame GPU-to-CPU readback.

## Primary Goal

Create a new plugin, based on `godot-cpp-template`, that provides:

- `libmpv`-backed video playback
- a Vulkan-oriented rendering path
- zero-copy, or as close to zero-copy as practical
- an API suitable for in-game 3D surfaces and VR environments
- Godot-routed per-channel audio suitable for spatial playback

## Success Criteria

The project is successful when all of the following are true:

- A Godot 4 project can place video on a 3D surface in-game on Windows.
- The playback path does not perform per-frame CPU readback like `glReadPixels()`.
- The plugin works in exported games, not just in the editor.
- Build and packaging are automated in GitHub Actions for Windows.
- Exported binaries are easy to drop into a Godot project as a normal addon.
- Runtime dependency handling is explicit and reproducible.
- Stereo source channels can be routed to separate Godot or Steam Audio spatial emitters.

## Non-Goals

These are not first-phase goals:

- Compatibility with Godot's compatibility/OpenGL renderer
- Mobile, Web, or console support
- Editor tooling beyond what is needed to test and integrate playback
- Perfect feature parity with desktop mpv
- Broad codec/distribution packaging work beyond what is needed for Windows/Linux desktop builds

## Product Direction

The plugin should be designed for a game/runtime use case first, not as a generic media editor widget.

Priority order:

1. Efficient video rendering on 3D surfaces
2. Stable playback controls and stream/file loading
3. Predictable build/export/install story
4. Additional mpv features such as track management, subtitles, and advanced properties

## Technical Requirements

### Rendering

- Treat zero-copy as a core design constraint, not an optimization pass.
- Avoid any architecture that depends on:
  - `glReadPixels()`
  - software-rendered frame upload every frame
  - creating a new Godot `ImageTexture` every frame
- Prefer a Vulkan-native or RenderingDevice-compatible design.
- If true zero-copy is not possible on all paths, keep the fallback paths explicit and separate from the main architecture.

### Playback Backend

- Use `libmpv` as the playback engine.
- Support local files and network streams.
- Keep the Godot-facing API small and stable at first:
  - `load_file`
  - `play`
  - `pause`
  - `stop`
  - `seek`
  - time/duration/state queries
- Additional features such as audio track selection, subtitles, and property passthrough can follow once rendering architecture is sound.

### Platform Targets

- Primary:
  - Windows x86_64
- Nice to have later:
  - Linux x86_64
  - Linux ARM64
  - macOS

## Delivery Constraints

- Build from `godot-cpp-template` rather than inheriting the current prototype codebase wholesale.
- Reuse lessons and isolated pieces from existing repos, but do not preserve an architecture that is centered on CPU frame transport.
- CI must produce downloadable prebuilt binaries for target platforms.
- Export behavior must be tested, not just editor loading.

## What To Reuse From Existing Work

Useful material to port selectively:

- CI and packaging lessons from `godot_mpv`
- Windows dependency staging and export packaging behavior from `godot_mpv`
- GDExtension/addon layout conventions from recent fixes in `godot_mpv`
- Player API ideas from `gdmpv`
- mpv property and command wrapping patterns from both prototypes

## What Not To Reuse As Foundation

Do not carry forward these architectural choices:

- GPU-to-CPU frame readback
- software frame rendering as the main display path
- per-frame recreation of `Image` / `ImageTexture`
- render flow designed around a `TextureRect` or editor/demo convenience rather than runtime renderer integration

## Open Technical Questions

These questions should be answered early in the new repo:

- What is the cleanest libmpv-to-Godot Vulkan interop path?
- Can libmpv render into a resource that Godot can sample directly through Godot 4's rendering APIs?
- What synchronization model is required between mpv rendering and Godot rendering?
- Is a Vulkan-only plugin acceptable for the first milestone?
- If a fallback path is needed, how can it remain clearly separated from the intended production path?

## First Milestones

### Milestone 1: Feasibility Spike

- Create a fresh plugin from `godot-cpp-template`
- Integrate `libmpv`
- Prove a renderer path that avoids per-frame CPU readback
- Limit scope to one platform if necessary to validate architecture

### Milestone 2: Minimal Runtime Player

- Expose a small playback API to Godot
- Render video onto a test 3D surface
- Validate behavior in an exported project

### Milestone 3: Packaging

- Add GitHub Actions builds for Windows
- Package runtime dependencies correctly
- Verify exported addon layout and runtime loading

### Milestone 4: VR Readiness

- Test with realistic VR scene load
- Measure frame timing impact
- Confirm the video path leaves enough headroom for the rest of the game

## Guiding Principle

This project should optimize for the correct rendering architecture and VR-safe playback behavior first. A smaller feature set with a viable GPU-native path and hitch-free transport is more valuable than a feature-rich player built on unavoidable CPU copies or blocking transitions.
