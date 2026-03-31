# godot-libmpv-zero Implementation Plan

## Context

This project creates a Godot 4 GDExtension for libmpv-backed video playback targeting VR use cases. The existing prototypes in this workspace (`godot_mpv` and `gdmpv`) confirm the same architectural problem: both move video frames through CPU memory, which is the wrong baseline for VR.

The charter already establishes the right product direction:

- fresh repo, not a prototype salvage job
- Vulkan-only for the first meaningful milestone
- Windows-first for development and validation
- zero-copy video path as the primary architecture
- audio routed through Godot for VR-spatial playback

For this project, "reasonable VR player" also means audio cannot remain a generic mpv desktop output. We want parity with the common VR pattern where each source channel can be routed to a separate 3D emitter, so a stereo video can behave like two virtual speakers in the world.

## Decisions

### Confirmed scope

- MVP target platform: Windows x86_64
- MVP renderer requirement: Vulkan only
- MVP VR target: OpenXR-enabled Godot projects
- MVP audio requirement: Godot-routed audio suitable for 3D placement

### Deferred scope

- Linux as a first-class packaged target
- renderer compatibility paths (OpenGL / Compatibility renderer)
- upstreaming mpv changes before the local product works
- feature depth beyond stable playback, texture output, and spatial audio routing

## Main Conclusions From Repo And Source Review

### 1. The current prototypes should only be mined for lessons

`godot_mpv` renders into an offscreen GL target, then uses `glReadPixels()` and recreates textures. `gdmpv` uses `MPV_RENDER_API_TYPE_SW` and uploads CPU buffers. Both are useful references for API shape, mpv event handling, CI, and packaging, but neither provides a viable runtime rendering foundation for VR.

### 2. Godot Vulkan interop is real, but render-thread constrained

Godot can wrap externally created GPU images with `RenderingDevice::texture_create_from_extension()`, and OpenXR already uses that path internally. However, the critical `RenderingDevice` calls involved in Vulkan interop are render-thread guarded. This means the plan cannot assume Vulkan handle extraction and texture wrapping happen from arbitrary extension code on the main thread.

This is the single most important architectural correction to the previous draft.

The Vulkan path must explicitly define:

- how render-thread work is scheduled
- which objects are owned by the render thread
- how main-thread Godot objects observe render-thread state safely
- how mpv and Godot synchronize access to shared Vulkan images

### 3. The mpv fork is a product dependency, not a temporary implementation detail

If we need both:

- a Vulkan libmpv render backend
- an audio callback or AO path suitable for Godot-owned playback

then our forked mpv becomes part of the product pipeline. The plan should assume:

- pinned fork revision
- automated Windows builds for the fork
- published build artifacts used by this plugin CI
- packaging of forked runtime binaries until or unless upstream adoption happens

### 4. Audio is a first-class engineering problem, not just a feature

The desired VR behavior is not "play audio somehow." The target behavior is closer to AVPro/VRChat style speaker routing:

- left and right channels should be independently routable
- each routed channel should feed a separate `AudioStreamPlayer3D`
- users should be able to place those emitters in the world like virtual speakers

That means the audio architecture should not collapse immediately into one mixed `AudioStreamGenerator` unless we intentionally support a simplified mode in addition to speaker routing.

### 5. External upstream work is informative, but not a dependency

There is relevant upstream discussion and draft work around:

- Vulkan support in libmpv render APIs
- callback-oriented audio extraction from mpv
- Godot audio buffering behavior

Those efforts are useful as references and worth monitoring, but this project should proceed as if none of them will land in time to help. Design choices should be based on local feasibility and maintainability, not on expected upstream movement.

## Revised Architecture

## Video Architecture

### Goal

Render mpv video output into Vulkan images that Godot samples directly, with no per-frame GPU-to-CPU readback.

### Core model

1. On the render thread, obtain Godot Vulkan handles through `RenderingDevice::get_driver_resource()`
2. Create or import Vulkan resources on the same logical device Godot uses
3. Expose those images to Godot using `RenderingDevice::texture_create_from_extension()`
4. Publish the resulting RID through `Texture2DRD`
5. Have mpv render into those images through a custom Vulkan libmpv backend
6. Use explicit synchronization between mpv rendering and Godot sampling

### Required render-thread design

The Vulkan path needs a dedicated render-thread service layer. That layer owns:

- Godot Vulkan handle extraction
- external image creation and destruction
- RD texture wrapping
- any `Texture2DRD` RID replacement work
- synchronization object lifecycle if those objects are tied to the Godot device context

The main thread should not perform raw `RenderingDevice` interop calls that are render-thread guarded. Instead, the main thread drives high-level state changes and receives stable results.

### Buffering strategy

Start with double buffering. Triple buffering can be added if synchronization pressure or pacing problems show up in practice.

Each video frame slot should track:

- `VkImage`
- Godot wrapped RID
- image layout/state assumptions
- synchronization primitives or fence/semaphore bookkeeping
- whether the image is currently writable by mpv or readable by Godot

## Audio Architecture

### Goal

Decode audio with mpv but let Godot perform spatial playback.

### Core model

1. Capture decoded PCM from mpv through a custom AO or callback-oriented audio path in the local mpv fork
2. Split decoded frames into logical output channels
3. Feed those channels into separate Godot audio streams
4. Let users attach those streams to separate `AudioStreamPlayer3D` nodes representing virtual speakers

### Initial routing target

The first supported routing mode should be stereo speaker routing:

- channel 0 -> left speaker stream
- channel 1 -> right speaker stream

That keeps parity with the intended VR behavior without over-designing for arbitrary surround layouts on day one.

### Godot-facing shape

Rather than exposing one `audio_stream` property only, the plugin should be designed around a small channel-routing surface, such as:

- `get_audio_channel_count()`
- `get_audio_stream_for_channel(channel_index)`

We can still offer a convenience mixed stream later, but it should not be the only model.

### Sync implications

This path introduces real sync work:

- mpv owns decode clocking
- Godot owns playback buffering and 3D positioning
- buffering depth will affect perceived A/V sync

The first implementation should optimize for stable playback over perfect lip sync. Sync instrumentation should be built in early so we can tune buffering with real data instead of guessing.

## Internal API Shape

The plugin should not center all behavior in one large Node class.

Preferred shape:

- `MpvCore`
  - owns mpv instance, event pump, commands, and state
- `VideoOutputBackend`
  - abstract interface for SW upload backend and Vulkan backend
- `AudioBridge`
  - owns channel extraction, buffering, and Godot audio stream publication
- `MPVPlayer`
  - thin Godot-facing Node wrapper over the above pieces

This keeps backend-specific lifecycle and threading concerns out of the user API layer.

## Phased Plan

## Phase 0: Vulkan Interop Feasibility Spike

This is now the first gate. Do not build the rest of the product before this works.

### Objective

Prove that a GDExtension can safely use Godot's render thread to:

- obtain Vulkan driver resources
- create or import external images on Godot's device
- wrap them as RD textures
- expose them as `Texture2DRD`
- display them on a mesh in a Godot scene

### Deliverable

A tiny test extension that shows a texture backed by an externally owned Vulkan image in a Godot scene, without involving mpv yet.

### Exit criteria

- runs on Windows
- works in a Vulkan Godot project
- survives multiple frames and cleanup without validation or lifetime issues
- demonstrates the render-thread handoff model we will use for the plugin

### Why this comes first

If this fails or becomes much more invasive than expected, the rest of the architecture must change. The main existential risk in this project is not basic mpv initialization or Node API design. It is whether a GDExtension can safely participate in Godot's Vulkan renderer using render-thread-safe external texture interop.

## Phase 1: Minimal Runtime Player With CPU Video And Godot Audio

This phase is intentionally not the final rendering architecture. It exists to prove the non-Vulkan parts of the product while the Vulkan backend is still under development.

### Objective

Build a minimal usable libmpv-based player with:

- playback lifecycle
- event handling
- state queries
- channel-routed Godot audio
- temporary CPU-backed video output

### Video path

Use software rendering or another clearly temporary CPU path, but isolate it behind `VideoOutputBackend`.

The point of this phase is not performance. The point is to validate:

- mpv lifecycle
- Godot API surface
- audio routing model
- export/runtime loading basics

### Audio path

Implement the first custom mpv audio extraction path in the local fork.

Support:

- stereo source routing to two separate Godot streams
- enough buffering and diagnostics to identify underruns and sync drift
- a simple Godot test scene with two world-space speaker nodes

### Exit criteria

- load file
- play / pause / stop / seek
- texture visible in-scene
- two separate Godot audio streams for stereo content
- exported Windows build loads and runs

## Phase 2: Vulkan Video Backend

This is the first production-oriented rendering milestone.

### Objective

Replace the temporary CPU video backend with the shared-device Vulkan path.

### mpv fork work

Add a Vulkan libmpv render backend in the local fork, likely modeled conceptually on the existing GPU libmpv path rather than treated as a trivial copy of the OpenGL backend.

Expected work areas include:

- public render API surface for Vulkan
- libmpv backend registration
- Vulkan/libplacebo context import against an externally provided device
- image target wrapping for mpv render calls

### plugin work

- render-thread service for Vulkan interop
- Vulkan image pool management
- mpv frame submission into shared images
- synchronization between mpv writes and Godot reads
- `Texture2DRD` publication and replacement policy

### Exit criteria

- no per-frame CPU readback
- no per-frame image/texture recreation
- stable playback on a 3D surface in a Windows exported build
- profiling confirms the CPU-upload path is gone

## Phase 3: Packaging And Build Pipeline

At this point, the product depends on a custom mpv fork. CI must reflect that explicitly.

### Objective

Automate artifact production for:

- plugin binaries
- forked mpv binaries
- packaged addon layout suitable for a Godot project

### Windows-first artifact plan

- GitHub Actions builds forked mpv Windows binaries
- those artifacts are versioned and published
- plugin CI consumes pinned mpv fork artifacts
- final addon package includes required runtime DLLs

### Exit criteria

- one reproducible Windows CI path from source to packaged addon
- documented runtime contents
- exported sample project works using packaged artifacts

## Phase 4: VR Validation And Audio/Sync Tuning

### Objective

Validate the product in the actual intended environment: a VR scene with real rendering load and virtual speaker placement.

### Focus areas

- frame pacing impact in VR
- A/V sync stability under realistic buffering
- audio channel placement semantics
- scene integration ergonomics

### Exit criteria

- video surface usable in-world
- stereo speaker routing behaves correctly
- performance is acceptable for continued productization

## Implementation Notes

## Godot-facing API, first draft

This should stay smaller than the previous draft until the backend stabilizes.

### `MPVPlayer` methods

```
load_file(path: String)
play()
pause()
stop()
seek(seconds: float)
get_time_pos() -> float
get_duration() -> float
is_playing() -> bool
get_texture() -> Texture2D
get_audio_channel_count() -> int
get_audio_stream_for_channel(channel_index: int) -> AudioStream
```

### Signals

```
file_loaded()
playback_finished()
position_changed(time: float)
video_size_changed(width: int, height: int)
audio_channels_changed(count: int)
```

### Explicitly not in first API cut

- deep mpv property passthrough surface
- subtitles polish
- full track management UI helpers
- generalized surround channel routing UI

## Proposed source layout

```
src/
  register_types.cpp/h
  mpv_player.cpp/h
  mpv_core.cpp/h
  video_output_backend.h
  sw_video_output.cpp/h
  vk_video_output.cpp/h
  render_thread_service.cpp/h
  audio_bridge.cpp/h
  channel_audio_stream.cpp/h
```

## Build And Dependency Strategy

## Plugin build

- Start from the current `godot-cpp-template` layout
- Keep CMake as the primary build system
- rename the template artifacts early so the repo stops looking like scaffolding

## mpv dependency

### Windows MVP

- build against a pinned custom mpv fork
- publish Windows build artifacts from GitHub Actions
- consume those artifacts in plugin CI and release packaging

### Linux later

Linux support should be postponed until the Windows path is stable. When added, Linux should also target the same pinned fork model rather than relying on unknown system `libmpv` behavior if fork patches are required.

## Risks

| Risk | Severity | Notes / Mitigation |
|------|----------|--------------------|
| Godot render-thread constraints complicate Vulkan interop | High | Phase 0 exists specifically to de-risk this before product work proceeds |
| Importing Godot's Vulkan device into mpv/libplacebo is more constrained than expected | High | Verify with the smallest possible shared-device spike before broad implementation |
| mpv Vulkan libmpv backend becomes larger than anticipated | High | Treat fork maintenance as planned product cost, not surprise work |
| Audio extraction path in mpv is awkward or fragile | Medium-High | Start with the smallest callback-capable AO design that exposes decoded PCM reliably |
| A/V sync drift between mpv decode and Godot playback | Medium-High | Add instrumentation and tune buffer depth before chasing perfect sync |
| Stereo speaker routing API proves awkward in Godot scenes | Medium | Build a concrete sample scene early and iterate from real usage |
| Windows packaging of forked mpv runtime becomes messy | Medium | Publish pinned artifacts and make the plugin consume exactly those binaries |

## Verification Plan

## Phase 0 verification

- external Vulkan image visible in-scene
- `Texture2DRD` wrapper survives frame updates and shutdown
- render-thread scheduling path is stable

## Phase 1 verification

- local file playback
- seek / pause / stop behavior
- two separate Godot streams for stereo audio
- 3D speaker test scene works in editor and exported Windows build

## Phase 2 verification

- no `glReadPixels()`
- no software-render main path
- no per-frame texture recreation
- profiling confirms no recurring CPU upload path for video

## Phase 3 verification

- CI builds plugin and forked mpv artifacts reproducibly
- packaged addon works in a fresh sample project

## Phase 4 verification

- OpenXR VR scene playback remains stable
- virtual speaker placement behaves correctly
- acceptable performance under realistic scene load

## Immediate Next Steps

1. Rename the template scaffold to real library and entrypoint names so the repo matches the project.
2. Build the smallest possible Phase 0 render-thread/Vulkan texture interop spike.
3. Decide the minimum mpv fork layout and CI artifact strategy before significant player code is written.
4. After Phase 0 succeeds, implement the minimal backend-separated player shell and audio channel routing path.
