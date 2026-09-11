# Linux development

The initial prototype uses a patched Godot 4.7.2 build. The patches enable the
Vulkan features required by libplacebo, track external texture writes, and retire
audio playback objects before unloading extension bindings. Revisit stock-engine
support after playback is reliable; the flake keeps an official unpatched build
as a control.

## Build and run

On x86_64 Linux with Nix flakes enabled and a working Vulkan desktop driver:

```sh
bash tools/setup_linux.sh
nix develop
LIBMPV_ZERO_MEDIA="$PWD/project/smoke_test_lr_sync.mp4" \
  godot --path project --rendering-driver vulkan --xr-mode off --disable-vsync --max-fps 60 \
  res://example_vr.tscn -- --flat
```

The setup script initializes the pinned `godot-cpp` submodule, builds and stages
Steam Audio, builds the extension, and imports the sample project. Subsequent
extension builds need only `nix develop --command bash tools/build_linux.sh`.
Use `LIBMPV_ZERO_BUILD_JOBS` to change the default six build jobs, and
`LIBMPV_ZERO_BUILD_DIR` to override the build directory. The default is in the
user's cache directory, so an NTFS source checkout does not contain compiler
outputs. Nix builds dependencies in its own build directories.

The flake pins the mpv fork, FFmpeg/libplacebo, Godot, Steam Audio addon and SDK.
Local patches live under `patches/`; the original fork checkout is not required.
The shell selects the custom libmpv through an absolute runtime override so an
installed stock libmpv cannot silently substitute for its custom APIs. Godot
uses shared libstdc++ to avoid C++ symbol conflicts with Steam Audio's SDK.
This environment does not replace the system Godot installation.

`LIBMPV_ZERO_MEDIA` accepts a local absolute path or URL. Desktop testing of the
VR scene uses `--xr-mode off` and `--flat`, which attaches a normal camera and
moves the Steam Audio listener to it. Headset validation is a separate step.
The desktop overlay provides pause, seeking, fullscreen and source entry; files
can also be dropped onto the window. Space toggles pause, arrows seek five seconds,
O opens source entry, and F toggles fullscreen.

mpv's built-in `ytdl_hook` finds the shell's `yt-dlp` executable. The shell also
includes Deno for current YouTube extraction. Subtitle/audio-track pickers and
the eventual shared playlist API still need a dedicated UI.

## Automated playback check

```sh
nix develop --command bash tools/test_linux.sh
LIBMPV_ZERO_TEST_CPU_QUOTA=150% nix develop --command bash tools/test_linux.sh
nix develop --command bash tools/test_linux.sh transport
nix develop --command bash tools/test_linux.sh consumer_stall
nix develop --command bash tools/test_linux.sh screen_lighting
nix develop --command godot --headless --path project --audio-driver Dummy \
  --xr-mode off --script res://tests/steam_audio.gd
```

The test runs the Steam Audio scene with Vulkan synchronization validation,
checks changing GPU-rendered frames and consumption of both audio channels,
and captures the actual stereo output through a temporary PulseAudio/PipeWire
null sink. It fails on validation errors, logged errors, leaks, silent channels,
nonfinite samples, clipping, or a timeout. Captures and a JSON report go into
ignored `.local/linux-smoke/`; override with `LIBMPV_ZERO_TEST_OUTPUT`.

The optional systemd user scope applies the CPU quota only to Godot and its
children. `150%` means one and a half logical CPU cores, not 150% of the whole
machine. Capture and the surrounding development tools stay outside that scope.
This checks scheduling pressure, not VR frame-budget compliance.
The runner disables vsync to avoid compositor throttling of background windows
and caps rendering at 60 FPS; override the cap with `LIBMPV_ZERO_MAX_FPS`.

`transport` covers pause, seek while paused, resume, backward seek, stop/reload,
and delayed playback-object release. `consumer_stall` pauses just the two Steam
Audio consumers: mpv's clock must stop once its bounded queue fills, then recover.
The headless Steam Audio test keeps old playback objects across player deletion
and compares irregular mixer requests against a direct source sample by sample.
`screen_lighting` pauses playback, changes scene lighting, and checks that the
screen pixels stay unchanged while the textured area light illuminates the room.
It disables bloom during that comparison to isolate direct lighting, then
restores the scene's glow for its final screenshot.

Generate a local fixture with moving video, simultaneous flashes/tones, a
center-channel-only 5.1 soundtrack, and a separate native ASS subtitle track:

```sh
nix develop --command python3 tools/make_test_media.py .local/fixtures/av-surround.mkv
LIBMPV_ZERO_MEDIA="$PWD/.local/fixtures/av-surround.mkv" \
  nix develop --command bash tools/test_linux.sh playback_timing
LIBMPV_ZERO_MEDIA="$PWD/.local/fixtures/av-surround.mkv" \
  LIBMPV_ZERO_TEST_RENDER_STALLS=1 LIBMPV_ZERO_TEST_CPU_QUOTA=150% \
  nix develop --command bash tools/test_linux.sh playback_timing
LIBMPV_ZERO_MEDIA="$PWD/.local/fixtures/av-surround.mkv" \
  LIBMPV_ZERO_TEST_SECONDS=600 LIBMPV_ZERO_TEST_TIMEOUT=660s \
  LIBMPV_ZERO_TEST_CPU_QUOTA=150% \
  nix develop --command bash tools/test_linux.sh playback_soak
```

`playback_timing` reads back the native video texture on every rendered frame.
It compares flash timestamps with tone onsets in software loopback capture;
capture delivery introduces latency, and this does not measure the physical
display, DAC or headset. It also exercises image-layout changes from readback.
The optional render stalls delay only Godot's main/render thread by 50, 100 and
200 ms between reference flashes, exercising recovery while audio keeps running.
`playback_soak` samples frames once per second, reloads at EOF, and records queue
depth, channel consumption, resident memory and frame gaps. Its memory-growth
guard is deliberately coarse; inspect trends as well as the pass/fail result.
The supported baseline uses Godot's default Safe render-thread mode.
`LIBMPV_ZERO_RENDER_THREAD=separate` remains a diagnostic option: that experimental
mode stalled playback and crashed during shutdown. The shutdown failure also
reproduced in a bare project without extensions. Do not use it for movie testing.

## Verified baseline (2026-09-11)

These checks passed on an NVIDIA Vulkan desktop using the pinned patched Godot
4.7.2, mpv 0.41.0 fork and Steam Audio SDK 4.8.1. Rendering used X11, Safe thread
mode, vsync disabled and a 60 FPS cap. They do not establish Windows or PCVR
compatibility.

| Check | Observed result |
| --- | --- |
| Ten-minute soak with a 150% CPU quota | Looped a 120-second 720p fixture through four EOF reloads; 593 changing-frame samples, aligned stereo consumption counters, queue at or below 4,736 of 4,800 frames. |
| Memory during that soak | About 45 MiB RSS growth after warmup, then no sampled RSS change in the final minute. |
| Frame pacing during that soak | Median 16.7 ms, p99 19.2 ms, maximum 299.8 ms. Reload transitions still need attention for VR comfort. |
| Thirty-second timing probe with CPU quota and deliberate stalls | Ten 50–200 ms stalls, 30 video flashes, 28 matches after startup exclusion; software video-minus-audio median +17.0 ms and maximum absolute offset 48.4 ms. |
| Transport and consumer interruption | Four pause/seek/stop/reload cycles and three consumer-only stall/recovery cycles passed under the CPU quota. |
| Steam Audio regression | Retired playback objects returned silence; arbitrary mixer block sizes preserved source samples without loss, duplication or accumulation. |
| Source support | Local native ASS subtitles were visually checked; center-only 5.1 audio reached both spatialized speakers; a YouTube URL played through mpv's built-in yt-dlp hook. |

The captured playback runs exited cleanly with no logged validation errors or
shutdown leaks under the test scene configuration described below. The timing
figures are a software probe, not an end-to-end latency measurement. The soak
used repeated short media, so a full-length movie remains a useful next test.

For further manual sessions, use the desktop launch command above with a movie path
or URL. Check dialogue timing, subtitle appearance, seeking, pause/resume and
behavior at EOF over an uninterrupted viewing session. Follow with headset
testing before preparing portable Linux and Windows builds for other testers.

### Initial VR viewing and screen lighting follow-up

A user watched a 24-minute episode in VR and reported no stalls or A/V desync,
with solid video and spatial audio. They did observe intermittent bright pixels
on or near the screen, amplified by bloom, including while playback was paused.
This is initial headset evidence, not broad PCVR compatibility validation.

The scene's existing textured `AreaLight3D` now provides the screen's response on
the room without a reflection probe. Rectangular LTC area lights shipped in
[Godot 4.7](https://godotengine.org/releases/4.7/); texture support and GPU costs
are documented in [AreaLight3D](https://docs.godotengine.org/en/4.7/classes/class_arealight3d.html).
This provides direct diffuse/specular screen lighting, not general reflections
of the room or bounced global illumination. The light follows video aspect ratio.

The authored screen material was lit even though the script's fallback material
was unshaded. The screen is now consistently unshaded, so it does not receive
its coplanar area light. Forward+ ignores emission on unshaded materials, so the
video uses albedo alone, with the texture's sRGB view handling color conversion.
The earlier extra forced sRGB conversion is disabled.

Desktop playback and the paused-screen lighting check pass without removing any
scene nodes for the tests. All 135 sampled screen pixels were unchanged when
lighting varied; the same check failed with the old material restored. Transport
regression and native ASS/surround playback also passed after the scene change.
The reported flashes have not been reproduced in
desktop diagnostics; their exact cause, including whether NaNs are involved,
remains unconfirmed. The material correction needs a headset retest of paused
and moving video before considering that report resolved.

## Implementation and remaining gates

The original mpv fork's independent audio timer has been replaced with a push
audio output driven by actual consumer queue depth. The PCM ring is bounded to
100 ms. Audio reset generations isolate retired playbacks after seeks, and the
delay estimate includes Godot's reported output latency. mpv downmixes surround
sources to the demo's two mono screen speakers before Steam Audio spatializes
them. The addon patches handle retired-player lifetime, fixed processing blocks,
and clearing reused output buffers. Steady playback still uses mutexes, and
Godot's extension-facing `mix_audio` call allocates a returned sample array.

Vulkan rendering now runs entirely on Godot's render thread. Godot transitions
and records each external texture write, and mpv returns the image in the agreed
layout after finishing GPU work. The prototype synchronously flushes and stalls
GPU work at this boundary. This establishes a correctness baseline; the cost
needs measurement in a headset before designing an asynchronous semaphore path.
Additional GPU/driver and Windows coverage must check queue ownership as well.
Playback properties arrive through mpv observations instead of synchronous reads
on the rendering thread. Render updates continue through load and seek transitions.

The original baseline tests omitted the scene's ReflectionProbe because Godot
4.7.2 leaked seven RD textures even without MPVPlayer. The scene now omits that
probe too, and the tests run the same scene. The separate intermittent audio-object
shutdown leak is addressed by the engine's early audio retirement patch.

Desktop playback, native ASS subtitles, surround downmix and a YouTube URL through
mpv's yt-dlp hook have been exercised. A full-length movie, instrumented physical
A/V sync, the remaining headset visual issue, other GPUs, HDR/color correctness, Windows and
distributable exports remain separate validation gates. The next API design stage
is the multiplayer theater integration after singleplayer testing.

The staged Linux libraries are development builds with Nix runtime paths.
They are not yet a portable release bundle. Existing Windows release history
does not validate the current dependency patches on Windows.
