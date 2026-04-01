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

### 5. The current audio bridge proved the concept, but not the production design

The current prototype successfully proves:

- forked mpv can decode and hand PCM to Godot
- stereo channels can be exposed as separate Godot-facing streams
- the sample scene can route those channels to independent 3D emitters

It also revealed the main limitation of the current approach:

- `AudioStreamGeneratorPlayback` driven from plugin-side polling is not a strong long-term sync model

Empirically, the prototype can play, but it still produces audible underruns under otherwise simple conditions. The root cause is architectural, not just "buffer is too small":

- mpv normally syncs around the AO's real playback delay
- the current `ao_godot` implementation paces itself on a timer thread, not a real device-backed delay model
- Godot's `AudioStreamGeneratorPlayback` is a ring buffer that zero-fills on underrun, but does not provide a pull-style playback clock to the producer
- the plugin currently feeds that ring buffer from game/plugin-side update logic, not from the audio mix thread

This means the first audio implementation should now be treated as a diagnostic prototype, not as the final architecture.

### 6. Godot's own video audio path points to the more maintainable direction

Godot's internal `VideoStreamPlayer` does not use `AudioStreamGeneratorPlayback` for its main audio path. Instead, it:

- registers an audio mix callback with `AudioServer`
- lets `VideoStreamPlayback` provide decoded audio through `mix_audio()`
- resamples and mixes audio from the engine audio thread

That is materially closer to what we need than the current generator-fed prototype. It suggests the long-term Godot-facing architecture should be pull-based on the audio thread, not push-based from `_process()`.

The `godot-steam-audio` plugin reinforces the same conclusion. Its core path wraps an underlying `AudioStream` in a custom `AudioStreamPlayback`, then applies Steam Audio processing inside `_mix()`. This is much easier to compose with than trying to spatialize a generator buffer from the outside after the fact.

### 7. External upstream work is informative, but not a dependency

There is relevant upstream discussion and scattered issue history around:

- Vulkan support in libmpv render APIs
- callback-oriented audio extraction from mpv
- Godot audio buffering behavior
- GDExtension and audio thread limitations

Those efforts are useful as references and worth monitoring, but this project should proceed as if none of them will land in time to help. Design choices should be based on local feasibility and maintainability, not on expected upstream movement.

As of April 1, 2026, a quick tracker scan did not surface an existing upstream Godot feature that cleanly exposes `AudioServer::add_mix_callback()` to GDExtension, nor an existing upstream mpv libmpv audio callback path that removes the need for our fork work.

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

Decode audio with mpv but let Godot perform spatial playback through a pull-style engine integration that is compatible with Steam Audio.

### Core model

1. Capture decoded PCM from mpv through a custom AO or callback-oriented audio path in the local mpv fork
2. Split decoded frames into logical output channels
3. Expose those channels through a Godot-native playback object that is consumed from the audio mix thread
4. Allow those channel streams to feed either plain `AudioStreamPlayer3D` or a Steam Audio-compatible playback wrapper

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

### Preferred integration direction

The intended production direction is:

- mpv fork exposes decoded PCM to the plugin
- plugin stores decoded PCM in per-channel queues owned by an audio-side playback object
- Godot consumes those queues from a pull-style playback interface on the mix thread

There are two viable ways to get there:

#### Option A: Engine patch

Patch Godot to expose an audio mix callback or similar pull-style hook to GDExtension. This is the smallest conceptual gap from Godot's own `VideoStreamPlayer` path and would likely be the cleanest long-term solution if accepted locally.

#### Option B: Plugin-owned custom playback type

Implement a custom `AudioStreamPlayback`-style object in the extension, similar in spirit to `godot-steam-audio`, and let that playback pull decoded PCM from plugin-owned queues during `_mix`.

This is likely the best fallback if an engine patch is too invasive or slow to maintain.

### Steam Audio compatibility target

Steam Audio is not a side quest. It changes what "good audio integration" means for this project.

The goal should be that each decoded source channel can be:

- routed to a plain `AudioStreamPlayer3D`, or
- wrapped in a Steam Audio-compatible playback path for HRTF, occlusion, reflections, or other spatial effects

The existing `godot-steam-audio` plugin already demonstrates a pattern of wrapping a source `AudioStream` inside a custom playback class and doing Steam Audio processing in `_mix()`. That strongly suggests our audio architecture should preserve compatibility with that model instead of locking itself to `AudioStreamGeneratorPlayback`.

### Sync implications

This path introduces real sync work:

- mpv owns decode clocking
- Godot owns playback buffering and 3D positioning
- buffering depth will affect perceived A/V sync

The current prototype proved that simply "having a buffer" is not enough. The production path needs:

- a pull-side playback clock or audio-thread consumption point
- explicit startup buffering policy
- explicit drain/end-of-playback policy
- instrumentation for decoded queue depth, mixer-side skips, and perceived A/V offset

The first production audio implementation should optimize for stable playback over perfect lip sync. Sync instrumentation should be built in early so we can tune buffering with real data instead of guessing.

## Internal API Shape

The plugin should not center all behavior in one large Node class.

Preferred shape:

- `MpvCore`
  - owns mpv instance, event pump, commands, and state
- `VideoOutputBackend`
  - abstract interface for Vulkan video backend(s)
- `AudioBridge`
  - owns channel extraction, buffering, and handoff into the Godot-side pull model
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

## Phase 1: Minimal Runtime Player With Temporary Video And Diagnostic Audio

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

This phase is now complete in spirit, even if the current code still needs cleanup and follow-up. It proved:

- forked mpv audio callback path works
- stereo routing works
- diagnostics are useful

It also proved that the generator-fed path should not be treated as the final design.

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

This phase is also effectively achieved for the current prototype:

- externally owned Vulkan images are wrapped successfully
- mpv renders into those images
- Godot samples them in-scene

The remaining video work is cleanup and productization, not existential feasibility.

## Phase 3: Production Audio Path And Steam Audio Compatibility

This is now the next core engineering phase.

### Objective

Replace the diagnostic generator-fed audio bridge with a pull-style audio path that is compatible with stable A/V sync and Steam Audio.

### Decision gate

Choose one of:

- engine patch exposing a usable audio-thread mix callback path to GDExtension
- plugin-owned custom playback implementation modeled after Godot's `VideoStreamPlayback` / `AudioStreamPlayback` patterns

### Focus areas

- use audio-thread consumption as the timing source
- preserve per-channel routing
- make startup/drain deterministic
- expose enough diagnostics to compare decoded queue depth with mixer-side skips
- validate that a channel can be wrapped or fed into a Steam Audio-compatible playback path

### Exit criteria

- no audible underruns in the stereo sync sample under normal load
- end-of-file drains cleanly
- per-channel routing still works
- at least one credible Steam Audio integration path is demonstrated
## Phase 4: Packaging And Build Pipeline

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

## Phase 5: VR Validation And Audio/Sync Tuning

### Objective

Validate the product in the actual intended environment: a VR scene with real rendering load and virtual speaker placement.

### Focus areas

- frame pacing impact in VR
- A/V sync stability under realistic buffering
- audio channel placement semantics
- Steam Audio spatialization behavior
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
  vk_video_output.cpp/h
  render_thread_service.cpp/h
  audio_bridge.cpp/h
  channel_audio_stream.cpp/h
  spatial_channel_stream.cpp/h
```

## Build And Dependency Strategy

## Plugin build

- Start from the current `godot-cpp-template` layout
- Keep CMake as the primary build system
- rename the template artifacts early so the repo stops looking like scaffolding

## mpv fork scope

The fork needs to add two things that do not exist in upstream mpv:

### 1. Custom callback / pull-capable AO driver

mpv's internal `ao_driver` interface has both push and pull modes. Pull-based AOs call
`ao_read_data()` when they are ready for samples. A new `ao_godot.c` driver would:

- operate in pull mode (implement `init`, `start`, `reset`, `uninit`)
- on `start()`, store a user-provided callback and begin calling it on a timer or
  dedicated thread whenever a new audio period is available
- use `ao_read_data()` to fill the period buffer, then hand decoded PCM to the callback

The callback setter must be exposed through the public libmpv API. The current local path
already uses a dedicated `mpv_godot_audio_set_callback(mpv_handle *, callback_fn, void *userdata)`
entry point added to the shared library and declared in an extension header.

The existing `ao_pcm.c` (writes decoded PCM to file) and `ao_openal.c` (pull-based via
OpenAL callback) are the closest reference implementations inside the mpv tree.

Current finding: the present `ao_godot.c` proves decode extraction, but because it paces
itself on a timer thread rather than a real playback-delay model, it should be considered
an intermediate step rather than the final sync design.

### 2. Vulkan libmpv render backend (Phase 2)

mpv currently exposes two render backends through the libmpv render API:
- `MPV_RENDER_API_TYPE_OPENGL` — implemented in `video/out/gpu/libmpv_gpu.c`
- `MPV_RENDER_API_TYPE_SW` — implemented in `video/out/libmpv_sw.c`

The `render_backend_fns` interface in `video/out/libmpv.h` is the extension point.
A new Vulkan backend would:

- register under a new `MPV_RENDER_API_TYPE_VULKAN` string (defined in a new
  `include/mpv/render_vk.h` header similar to `render_gl.h`)
- accept an externally provided `VkInstance`, `VkPhysicalDevice`, `VkDevice`, and queue
  family index at init time — these come from Godot's rendering device
- use libplacebo's `pl_vulkan_import()` to create a `pl_gpu` context on that device
  (libplacebo already supports device import; this is how the gpu_next VO works internally)
- implement `render()` by accepting a `VkImage` target and layout, rendering a frame
  into it, and returning a `VkSemaphore` or fence the caller can wait on before sampling
- implement `get_image()` to optionally allocate from the external device's allocator

This is the most substantial fork work. The gpu_next VO (`video/out/gpu_next/`) and
the libplacebo Vulkan context (`video/out/vulkan/`) are the reference implementation.
libplacebo's `pl_vulkan_import` path is already tested in production (e.g. by
external GPU-compute integrations); the main new surface is the libmpv API wrapper.

## mpv dependency

### Local development build (Windows, MSYS2)

MSYS2 is the most practical local build environment on Windows. mpv's own CI uses
this path and documents it in `DOCS/compile-windows.md`.

Minimal build for plugin development (`MSYS2 CLANG64` shell, or `bash` with `/clang64/bin` on `PATH`):

```bash
# Install base tools in MSYS2
pacman -S --needed base-devel git
# Install CLANG64 toolchain and dependencies
pacboy -S --needed python pkgconf cc meson ffmpeg libjpeg-turbo libplacebo luajit vulkan-headers nasm yasm cmake
```

Then in the mpv fork checkout:

```bash
meson setup build \
  -Dlibmpv=true \
  -Dcplayer=false \
  -Dvulkan=enabled \
  -Dgl=disabled \
  -Dd3d11=disabled \
  --prefix=$(pwd)/install

ninja -C build
ninja -C build install
```

This produces `install/bin/libmpv-2.dll` and `install/lib/libmpv.dll.a` plus headers.
The `--prefix` layout matches what the plugin's CMake already expects for the local dev
dependency path.

The `ci/build-win32.ps1` script in mpv's own repo shows an alternative native Windows SDK
build using Clang and Meson subprojects (no MSYS2 required), which may be useful for
CI environments where MSYS2 is inconvenient.

### CI artifacts (forked mpv-winbuild-cmake)

[shinchiro/mpv-winbuild-cmake](https://github.com/shinchiro/mpv-winbuild-cmake) is the
canonical approach for producing fully-featured, redistributable Windows mpv binaries.
It is a CMake ExternalProject system that cross-compiles the full dependency tree
(ffmpeg, libplacebo, shaderc, spirv-cross, vulkan, etc.) from Linux using a
MinGW-w64 toolchain it bootstraps itself.

**How to point it at our fork:**

The mpv package is defined in `packages/mpv.cmake`. The only required change is:

```cmake
GIT_REPOSITORY https://github.com/our-org/mpv.git   # was mpv-player/mpv.git
GIT_TAG        our-branch-or-commit-hash
```

The full dependency graph (libplacebo, shaderc, vulkan, ffmpeg, etc.) is already
defined and working. Adding `GIT_TAG` pins the build to a specific commit so CI
artifacts are reproducible.

**Fork and workflow strategy:**

1. Fork `mpv-winbuild-cmake` into our org.
2. Change `packages/mpv.cmake` GIT_REPOSITORY and add a pinned GIT_TAG.
3. Add a GitHub Actions workflow (`.github/workflows/build.yml`) that:
   - runs on `ubuntu-latest`
   - installs the prerequisites listed in the README (ninja, cmake, meson, nasm, etc.)
   - runs `cmake -DTARGET_ARCH=x86_64-w64-mingw32 -G Ninja -B build64 .`
   - runs `ninja gcc` once to build the toolchain (cache this layer aggressively)
   - runs `ninja mpv` to produce artifacts
   - uploads `mpv-dev-x86_64-*` (contains `libmpv-2.dll`, `libmpv.dll.a`, headers)
     as a versioned release artifact

The toolchain bootstrap (`ninja gcc`) takes ~20 minutes and should be cached between
runs using the Actions cache keyed on the toolchain commit. Once cached, incremental
`ninja mpv` builds are fast.

**Artifact layout produced by mpv-winbuild-cmake:**

```
mpv-dev-x86_64-YYYYMMDD/
  libmpv-2.dll
  libmpv.dll.a
  include/
    mpv/client.h
    mpv/render.h
    mpv/render_gl.h
    mpv/stream_cb.h
```

We need to also copy our extension headers (`render_vk.h`, `godot_audio.h`) in the
`copy-binary` step.

**Plugin CI consumption:**

The plugin's CMake finds these artifacts via `MPV_DIR` pointing at the extracted
artifact directory. This is already the pattern used for the local dev build.
In CI, a workflow step downloads and extracts the pinned mpv-dev artifact before
the plugin build runs.

### Linux later

Linux support should be postponed until the Windows path is stable. When added, Linux
should also target the same pinned fork model. mpv-winbuild-cmake itself runs on any
Linux host and supports `aarch64-w64-mingw32` as a third target arch if needed. For
native Linux libmpv builds, distro packages or meson subproject builds are viable since
the dependency management problem is simpler than Windows.

## Risks

| Risk | Severity | Notes / Mitigation |
|------|----------|--------------------|
| Godot render-thread constraints complicate Vulkan interop | High | Phase 0 exists specifically to de-risk this before product work proceeds |
| Importing Godot's Vulkan device into mpv/libplacebo is more constrained than expected | High | Verify with the smallest possible shared-device spike before broad implementation |
| mpv Vulkan libmpv backend becomes larger than anticipated | High | Treat fork maintenance as planned product cost, not surprise work |
| Audio extraction path in mpv is awkward or fragile | Medium-High | Keep the current forked callback path, but move final sync responsibility to a pull-style Godot playback integration |
| A/V sync drift between mpv decode and Godot playback | High | Current generator-fed prototype already shows audible xruns; treat pull-style playback as the mitigation, not just larger buffers |
| GDExtension lacks a clean audio-thread hook | High | Investigate a small local Godot patch versus plugin-owned custom playback object |
| Steam Audio integration fights the chosen Godot audio surface | High | Align with custom playback / `_mix()` patterns instead of generator-only streams |
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

- audio is consumed from a pull-style playback path, not only `AudioStreamGeneratorPlayback`
- stereo sync sample plays without audible xruns
- EOF drains cleanly
- at least one Steam Audio integration experiment succeeds or is ruled out with evidence
## Phase 4 verification

- CI builds plugin and forked mpv artifacts reproducibly
- packaged addon works in a fresh sample project

## Phase 5 verification

- OpenXR VR scene playback remains stable
- virtual speaker placement behaves correctly
- acceptable performance under realistic scene load

## Immediate Next Steps

1. Clean up the current prototype code so the repo surface matches the Vulkan-only architecture already proven locally.
2. Investigate the best production audio integration path:
   engine patch for audio-thread hooks vs plugin-owned custom playback object.
3. Prototype a pull-style per-channel playback path that can coexist with Steam Audio expectations.
4. Once the production audio path is chosen, return to packaging and VR validation with that architecture fixed.

