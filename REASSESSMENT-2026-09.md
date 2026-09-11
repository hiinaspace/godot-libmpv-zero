# September 2026 reassessment and theater plan

Evaluated 2026-09-10. This is the current planning entry point; `PLAN.md` and
`CHARTER.md` retain the history of the Windows prototype. The assessment below
records the starting state; [Linux development notes](LINUX.md) track the later
bring-up and its remaining validation gaps.

## Recommendation

Continue `godot-libmpv-zero`. None of the alternatives inspected supplies the
combination of Linux + Windows, Vulkan scene textures without per-frame GPU
readback, mpv/yt-dlp media support, and Godot-routed audio needed here. There is
useful new upstream work to learn from, but no demonstrated replacement worth
switching to now. This is a source/documentation comparison, not a performance
bakeoff of competing binaries.

The next deliverable should be a reproducible two-platform video-player slice
on Godot 4.7.2, followed by a two-person theater. Avoid turning that first slice
into an engine rewrite or a general social-VR platform.

Working scope and proposed first-slice boundaries:

- Linux and Windows x86_64 desktop and PCVR from the first usable release.
- yt-dlp sites, direct HTTP media/HLS, and local-file playback. Start synchronized
  viewing with seekable VOD; live timelines need a separate synchronization policy.
- One static room, one screen, mpv-rendered subtitles and audio/subtitle track selection,
  queue/playlist controls, spatial voice, and head/hands avatars.
- First manual test: the maintainer plus one or two friends. Grow toward the recorded
  2–8-person theater scope; approximately 16 is the voice library's target, with
  32 remaining a characterization workload.
- Standalone Quest, UGC, full avatar import/IK, environmental acoustic simulation,
  media redistribution, and automatic host failover are outside the first slice.

## Inspected baseline

| Component | Verified state | Consequence |
|---|---|---|
| Godot baseline | `4.7.2.stable.nixpkgs.ed1daf0bf` | Already matches the current stable release and the voice project's API generation. [Release](https://github.com/godotengine/godot/releases/tag/4.7.2-stable) |
| mpv baseline | `0.41.0`, libplacebo `7.360.1`, FFmpeg `9.0.1` | Useful desktop baseline, but stock libmpv cannot replace the custom runtime. `0.41.0` remains the latest stable mpv release, dated December 2025. [Release](https://github.com/mpv-player/mpv/releases/tag/v0.41.0) |
| Player repository | Clean at `04b0049`, 2026-04-19 | More complete than a feasibility stub; the last commit materially changed texture ownership. |
| C++ bindings | `4862a9d`, bundled API JSON targets Godot 4.6 | A live 4.7.2 API dump has unchanged entries for the inspected rendering/texture/audio methods. This is encouraging, not a build or runtime compatibility test. |
| Custom mpv | branch `codex/godot-audio-callback`, `970250ad1480e78ca03999e1733d212278f07dbb` | Same SHA verified on GitHub. Three focused commits add audio callbacks, Vulkan rendering, and AO pacing. The fork's default `master` branch is not this implementation. [Branch](https://github.com/hiinaspace/mpv/tree/codex/godot-audio-callback) |
| Voice | `godot-network-audio`, clean at `dd202fe` | Godot API 4.7; Iroh 1.1; NetEq 0.9.1 fork pinned at `69c6ccb`; documented synthetic and headless spatial/churn checks. Current hardware/PCVR and Windows runtime success are not established by those checks. |
| Lobby | `iroh-lobby-spike`, `9382ce6` | A DHT benchmark, not a reusable Godot lobby addon. Cargo still references an absolute local Iroh checkout and moving Git branches. |
| Steam Audio addon | The local sample includes `stechyo/godot-steam-audio` node types and Linux/Windows descriptor entries | Upstream describes working Linux/Windows support but also alpha status and reduced maintenance. Required for the initial demo; ordinary spatial players are diagnostic controls. [README](https://github.com/stechyo/godot-steam-audio) |

The inspected Godot methods include `texture_create_from_extension`,
`texture_get_native_handle`, `get_driver_resource`, `call_on_render_thread`,
`Texture2DRD.set_texture_rd_rid`, and the audio `_mix`/`_mix_resampled` methods.
The engine ran only to dump its API. No video, audio, export, or headset test
was performed in this assessment.

### User decisions after review (2026-09-10)

- Use a small custom Godot build for the initial prototype to enable the Vulkan
  features required by libplacebo. Revisit stock-engine support after playback
  is reliable.
- Steam Audio/HRTF is required from the initial Linux demo. Ordinary Godot
  spatialization is only a diagnostic control. Patching the Steam Audio addon
  is an accepted maintenance cost.
- mpv's subtitle rendering is a principal reason to retain it; (L)GPL is
  acceptable for this project.
- Audit the fork's Vulkan exposure and audio callback design explicitly. Keep a
  reproducible baseline, but do not preserve earlier agent-made technical choices
  merely because they once worked.
- Sequence: Linux demo with Steam Audio -> automated rendering/audio stress and
  soak -> the user watches a movie alone -> optional singleplayer friend builds
  -> multiplayer/prim. Co-design media, voice and networking APIs during that
  integration instead of freezing speculative interfaces beforehand.
- Trusted friends with a shared lobby code are the admission model. Host transfer
  and elaborate permissions are not priorities.
- For NTFS source checkouts, use build directories on a native Linux filesystem.
  Moving a checkout is conditional on actual filesystem problems.

### Corrections to the old documentation

The April 19 implementation creates **Godot-owned RD textures**, obtains their
native Vulkan handles, and asks mpv to render into them. It no longer uses the
old external-image allocation/wrapping path described in much of `PLAN.md`.
`render_thread_service.cpp` still contains unused Windows Vulkan allocation
helpers. Remove or clearly isolate those helpers instead of porting dead code.

The two active runtime-loading paths in `mpv_core.cpp` and `vk_video_output.cpp`
still use Win32 DLL APIs and explicitly reject other platforms. The extension
descriptor contains only Windows entries. CMake stages DLLs only; the SCons file
still contains `EXTENSION-NAME` from the template. The existing build workflow
selects the latest successful dependency artifact rather than an immutable
dependency manifest, and the dependency builder defaults to upstream master
unless explicitly overridden. A successful build alone can therefore select
the wrong mpv variant.

Audio already uses custom pull-based `AudioStreamPlayback` objects, so there is
no need to implement the old plan's generator-to-pull migration again. However,
`AudioBridge` still uses a mutex and per-channel deques, primes 4,800 frames
(100 ms at 48 kHz), and discards the callback's `end_time_ns`. The fork's AO is
timer-paced and receives no actual Godot sink-latency/consumption feedback.
Reliable long-session A/V timing remains an engineering question.

## Alternatives and upstream changes

| Candidate | Current evidence | Fit for this theater |
|---|---|---|
| Godot's built-in video | Core still supplies Theora playback and does not support streaming URLs. [Docs](https://docs.godotengine.org/en/stable/tutorials/animation/playing_videos.html) | Does not cover the media-source requirements. |
| `VersaYT/godot_mpv` | Head `e2143ad`, March 17. Current code still calls `glReadPixels` and creates image textures. [Source](https://github.com/VersaYT/godot_mpv/blob/e2143ad78ad2/native/src/godot_mpv/mpv_player.cpp) | mpv controls are useful reference material; switching restores the CPU round trip this project was created to avoid. |
| `claytercek/godot-native-video` | Head `4fa0d0a`, July 23. Hardware decoding with GPU sharing on macOS Metal and Windows D3D12; no Linux backend. Windows Vulkan falls back to CPU readback. [README](https://github.com/claytercek/godot-native-video/tree/4fa0d0af19b5) | Genuine progress, but wrong platform/rendering coverage and narrower media scope. |
| `buresu/godot-avbridge` | Head `247b234`, June 15. Windows/macOS/Linux native backends; README explicitly documents NV12 readback followed by Godot texture upload. [README](https://github.com/buresu/godot-avbridge/tree/247b234e8b4a) | Promising general playback addon, not current GPU-sharing parity. |
| `xiSage/godot-vlc` | Head `7d4a000`, June 14. Godot audio callbacks, tracks/subtitles and network sources. GPU code is guarded by `gpu && windows`; Linux uses image uploads. [Source](https://github.com/xiSage/godot-vlc/blob/7d4a000dcdb9/src/vlc_media_player.rs) | Closest broader functional overlap. Still needs a Linux GPU path and separate yt-dlp integration. |
| `EIRTeam.FFmpeg` / `KirbyKid256/FFmpegPlayer` | EIRTeam head April 5; KirbyKid head July 2025. EIRTeam has CPU image updates and YUV-plane uploads; these projects expose FFmpeg playback rather than mpv's full source-resolution stack. [EIRTeam source](https://github.com/EIRTeam/EIRTeam.FFmpeg/blob/0b21297b3c3a/ffmpeg_video_stream.cpp), [FFmpegPlayer](https://github.com/KirbyKid256/FFmpegPlayer) | Reasonable simpler-file-player options, but no verified end-to-end replacement for this requirement. |

This does not establish that CPU upload makes a single 1080p movie unusable.
That is measurable, especially on a 4090. It means those alternatives do not
meet the existing rendering contract, and switching would trade away completed
work without clearly removing the remaining integration work. Reconsider a
maintained simpler backend if the native path cannot pass the bounded tests
below; benchmark actual VR frame times before making that trade.

Upstream mpv master, inspected at `7e4cb538a3f3`, still advertises only OpenGL
and software render backends in its public render API. Standalone mpv's Vulkan
output is a different interface. There is still no public equivalent of this
fork's `mpv_godot_audio_set_callback`. [Render API](https://github.com/mpv-player/mpv/blob/7e4cb538a3f3/include/mpv/render.h),
[audio-frame request](https://github.com/mpv-player/mpv/issues/9283)

Relevant upstream proposals were checked through the GitHub API, including
their merge status:

- [#16818](https://github.com/mpv-player/mpv/pull/16818): open, unmerged gpu-next
  render backend; the proposed initial backend is OpenGL.
- [#17828](https://github.com/mpv-player/mpv/pull/17828): open, unmerged libplacebo
  rendering RFC; its author reports hardware-frame limitations.
- [#18258](https://github.com/mpv-player/mpv/pull/18258): July Vulkan RFC, closed
  **without merging**. It adds caller wait/signal semaphores and reports Linux
  NVIDIA testing in mpvpaper. Read its implementation and review as design input;
  that report is not Godot validation or a maintained dependency guarantee.

Preserve the known fork revision for the first Linux reproduction. Then apply
the focused patches to an explicitly pinned, reviewed upstream revision and
rerun both platform gates. The current stock package's FFmpeg 9 compatibility
does not establish the April fork's compatibility with it. If necessary, use a
package-local compatible FFmpeg while repairing/rebasing the fork; do not change
the machine's global multimedia stack to accommodate the prototype.

### yt-dlp

mpv includes the Lua `ytdl_hook`, which invokes an external yt-dlp. This also
works in a suitably built/configured libmpv; it is not necessary to invent a
second extraction engine. Build with Lua, explicitly configure the hook/helper
path, and test a webpage URL through the extension. The standard libmpv profile
disables reading the user's mpv configuration, so desktop mpv settings are not
evidence that the embedded runtime is configured correctly.
[Hook](https://github.com/mpv-player/mpv/blob/7e4cb538a3f3/player/lua/ytdl_hook.lua),
[profile](https://github.com/mpv-player/mpv/blob/7e4cb538a3f3/etc/builtin.conf)

The latest yt-dlp release checked was `2026.08.19`. Full YouTube support now also
requires `yt-dlp-ejs` and a supported JavaScript runtime; upstream recommends
Deno. Verify whether the chosen binary already includes EJS and package the
remaining pieces on each OS. Keep the helper version independently replaceable
and log it in diagnostics. [Release](https://github.com/yt-dlp/yt-dlp/releases/tag/2026.08.19),
[dependencies](https://github.com/yt-dlp/yt-dlp#dependencies)

## Implementation sequence and acceptance gates

### 1. Reproduce the player on Linux and Windows

**Outputs:** project-local Nix flake/lock and dev shell, one supported CMake
workflow, pinned mpv source/build options, and two-platform debug/release addon
artifacts. Keep the present Windows artifact available as a comparison.

- Supply CMake, Ninja, Python, Meson/pkg-config, a C++ toolchain, Vulkan
  headers/loader, Lua, libplacebo, libass, FFmpeg, and yt-dlp/runtime through the
  local build definition. Treat exact package attributes and fork compatibility
  as build work to verify, not assumed working commands.
- Target Godot 4.7.2 and matching export templates. First compile the existing
  bindings to minimize simultaneous changes; then pin a supported godot-cpp
  revision/API target. The whole theater requires at least 4.7 because of voice.
- Centralize dynamic loading: `dlopen`/`dlsym` on Linux, Win32 APIs on Windows.
  Use one identified custom libmpv for both core and render calls, validate its
  custom capability/ABI, and report its full resolved path and revision.
- Preserve the existing Godot-owned texture path. Remove unused external
  allocation helpers and replace handwritten `mini_*` ABI declarations with
  pinned real headers or mechanically checked equivalents.
- Add Linux descriptor entries, output-directory creation, shared-library output
  rules, and `${CMAKE_DL_LIBS}` where applicable. Retire or fix stale SCons.
- Force the Vulkan driver in the sample and exports on both OSes. The generic
  Forward+/Mobile renderer selection is not a sufficient driver guarantee.
- Create a plain desktop scene with normal `AudioStreamPlayer3D` speakers and
  explicit flat launch support. The primary Linux demo must use Steam Audio;
  ordinary spatial players are useful only as an audio diagnostic control.
- Stage export dependencies outside the PCK where native loaders/helpers can
  access them. Support absolute/local paths and extracted packaged resources;
  `res://` is not inherently an mpv-readable filesystem path inside an export.

**Pass:** exported Linux and Windows apps play the included L/R test clip, an
HTTP/HLS VOD, and a public yt-dlp webpage URL, with visible changing video and
correctly routed sound. The same output works without developer PATH entries.
For NixOS use a closure/wrapper with explicit runtime paths; separately test a
portable Linux artifact on a conventional distro with a chosen glibc baseline.
Record dependency SHAs, hashes, features, helper versions, and notices in the
bundle. Headless import success is only a loader check.

### 2. Establish rendering and audio correctness under load

**This is the highest-risk phase.** It should precede elaborate networking/UI.

Rendering work:

- Trace CPU/render-thread time and GPU work in the present implementation.
  The fork calls `pl_gpu_finish()` in `done_frame`; GPU-native presentation does
  not imply asynchronous execution or a hitch-free render thread.
- Document the complete handoff between Godot and libplacebo: actual enabled
  device features/extensions, queue access, texture layouts, write completion,
  and completion of Godot sampling before reuse. The fork currently supplies
  fallback feature assumptions when none are passed; queried hardware support
  alone is not proof those features were enabled on Godot's device.
- Keep create/render/free operations serialized. Audit main/render-thread shared
  state and shutdown: invalidating a weak lifetime token is not a completion
  barrier for an already-running callback. Replace update-count retirement and
  abandoned service ownership with a completion-based lifetime protocol.
- Only remove the blocking GPU finish after an actual synchronization path is
  proven. Do not assume stock GDExtension exposes arbitrary Godot queue semaphore
  injection. If a small engine API addition is required, make that a separate
  decision based on a measured blocker.
- Verify orientation, aspect ratio, limited/full range and sRGB handling with
  reference clips. Begin with correct SDR and define HDR-to-SDR behavior before
  claiming HDR support.
- Measure hardware decode separately. The plugin currently sets no explicit
  `hwdec`; no-readback output does not prove zero-copy hardware decoding. Test
  software decode first, then NVDEC/Vulkan interop on representative Linux and Windows hardware. Record the selected decoder and every upload/copy path;
  keep unsupported codecs on an explicit fallback. [mpv hwdec options](https://github.com/mpv-player/mpv/blob/7e4cb538a3f3/DOCS/man/options.rst)

Audio work:

- Measure long-run A/V offset, queue occupancy, underruns, L/R alignment, and
  pause/seek/reload recovery. Account for Godot's output latency and sample-rate
  conversion rather than using mpv `time-pos` as the audible-time measurement.
- Give the AO actual consumption/buffer-delay feedback, or establish one shared
  playout clock with equivalent timing information. Refine after measuring the
  existing behavior; the intended result is bounded A/V drift over a whole movie.
- Remove long mutex holds/allocations from the mixer path where measurements or
  the ownership audit require it. Prevent muted, stopped, or distant speakers
  from independently changing channel timeline consumption.
- Mix voice and movie through Godot with separate buses and gain controls.
  Preserve per-source spatialization and headroom. Validate Steam Audio/HRTF
  from the initial demo, including its wrapper's buffer sizes and playback clock;
  patch that addon if needed. Baked acoustics need not block playback.

**Pass:** a 30–60-minute clip plus repeated seeks, reloads, resolution/track
changes, minimize/refocus, and scene teardown on both OSes. Capture actual output
audio and video timing, resource growth, and p95/p99/max frame times. Add video to
the voice workload and compare against a no-video control. Use the corrected
voice harness; its old Pulse null-sink startup artifact is already documented.

Then validate Linux and Windows OpenXR: headset/controllers/audio readiness,
readable screen in both eyes, spatial sound and microphone, and frame timing
within the chosen refresh budget (11.1 ms at 90 Hz). GPU render validation,
audio playback, controller input, and in-headset comfort are separate checks.

Scope stress to the test process: scripted main-thread stalls, scene/shader load,
CPU affinity and a dedicated user-systemd service with CPU/memory limits. Keep
builds and agent processes outside that cgroup. Start without whole-machine
stress or global network shaping; use a local throttled media server for loading
and buffering tests. Save baseline/stress timing, audio capture and resource data.

### 3. Manual singleplayer movie watch and friend builds

After the automated gate, the user watches a full movie with Steam Audio and
rendered subtitles. Provide useful controls and diagnostics for that test, then
prepare Linux/Windows singleplayer artifacts for friends if desired. Publishing
is a later user action. Require this feedback before the multiplayer stage.

The API items below are candidate requirements to co-design during phase 4,
not a separate interface-design prerequisite:

Keep `MPVPlayer` + `MPVTexture` and add the missing primitives before building
the network controller:

- Structured loading/ready/buffering/playing/paused/seeking/ended/error states;
  seekable range, buffered range, duration and local playout time.
- Playback-rate control for small drift corrections, asynchronous command results,
  and source-generation IDs so obsolete events cannot affect a newer load.
- Track enumeration/selection, subtitle visibility/delay, and external subtitle
  loading; expose semantic language/title metadata as well as local mpv IDs.
- Source-resolution status, cancellation/timeouts, bounded retries, and useful
  errors when yt-dlp, authentication, codecs or media availability fail.

mpv should handle decoding, demuxing, ASS/subtitle rendering and source hooks.
The application owns playlist policy. Begin with one mpv instance and one active
item; background preloading can otherwise double GPU/memory/decoder usage.

**Pass:** local scripts exercise the state transitions, rate/seek controls,
subtitle rendering and track changes on both OSes, including failed/cancelled
loads. The UI receives enough information to distinguish loading from a stall.

### 4. Build the two-person session and synchronize viewing

Put theater/game logic in the integration project (`prim` in the project note),
keeping this repository a media addon and `voice-core` transport-agnostic.

Extract the useful lobby-code/DHT logic from the spike into a small library;
replace absolute Iroh paths and moving patches with the same pinned dependency
set as the voice transport. The recorded ~5-second visibility is previous
benchmark evidence, not a fresh Internet guarantee. Handle delayed/stale lookup,
expiry, cancellation and connection retries in the join UI.

Use a session-owned Iroh endpoint/router and connection lifecycle. The current
voice service owns its endpoint/accept loop and treats datagrams as voice packets;
it does not yet supply generic Godot multiplayer replication. Add an explicit
shared-session interface or distinct routed protocols. Preserve its direct
network-to-audio packet path and monotonic receive timestamps.

| Data | Delivery | Ownership |
|---|---|---|
| Membership, queue edits, play/pause/seek, snapshots | Reliable ordered control stream | Host-authoritative session |
| Head/hands/root poses | Bounded unreliable sequenced datagrams | Peer-owned poses with interpolation |
| Encoded voice | Unreliable datagrams with separate routing/type | Existing Opus/NetEq path |
| Movie bytes | Each viewer's own source fetch | Local mpv; not voice or control transport |

Synchronize a timeline, not a sequence of simultaneous `play()` calls:

1. Host sends `session_epoch`, `revision`, stable queue/media ID, canonical source,
   paused state, rate, media position and a host-monotonic timestamp.
2. Peers estimate host clock offset with ping/pong and reject stale revisions.
   The playing target is `anchor_position + rate * (host_now - anchor_time)`.
3. On a new VOD, resolve and buffer locally, report ready, then start at a future
   host timestamp with a bounded readiness wait. Late joiners receive a snapshot.
4. Compare the target against local playout. Use a deadband, small temporary speed
   correction for modest drift, and a seek for large drift or explicit seeks.
   Suggested initial tuning: ~50 ms deadband, up to ±1% speed correction, seek
   above ~500 ms; these are test settings, not guaranteed outcomes.
5. For a lagging peer, display buffering and let it catch up locally by default.
   Offer an explicit group pause; one bad connection should not create an endless
   shared seek/pause loop. Late/duplicate commands and EOF are revision-checked.

Share canonical webpage URLs/content identity, not another viewer's expiring or
IP-bound extraction URLs. Each peer runs its own hook; verify timeline/duration
agreement and handle differing editions or unavailable content visibly. Local
files need an explicit per-peer match (hash/content ID); a host filesystem path
does not share the file. Live HLS needs common segment/program-time alignment,
so it is a follow-on to synchronized VOD, even if it already plays locally.

The room host serializes enqueue/remove/reorder/skip and distributes snapshots.
Start with everyone allowed to enqueue and the host controlling playback;
preferences such as volume and subtitle language are local. Do not replicate
raw mpv track IDs across independently resolved sources.

DHT rendezvous uses the shared-code/trusted-friends model: possession of the code
is sufficient admission. Retain session IDs/revisions to discard stale state;
host transfer and a separate permissions system are deferred. Initial unexpected
host loss may end the room cleanly. During integration, review open-source VRChat
synced players and Syncplay for clock, buffering and source-identity subtleties.
P2P also still uses rendezvous/relay services where necessary, and each viewer
needs bandwidth/access to the original media.

**Pass:** two exported clients, one Linux and one Windows, across different
networks. Join by code, hear each other, play a YouTube VOD and subtitled movie,
seek/pause/queue the next item, late join/reconnect, and recover one viewer's
temporary buffering. Measure inter-client playout error with output evidence,
not just reported positions. A useful initial target is <100 ms steady-state
skew after settling; validate A/V sync on each client independently.

### 5. Finish the theater interaction and ship the friend test

- One static room and screen; desktop movement plus OpenXR locomotion, minimal
  head/hands meshes and name labels. Smooth remote poses without delaying voice
  through the pose interpolation queue. Full humanoid IK/liltoon is unnecessary.
- One UI model for desktop controls and an in-world panel: paste URL, queue,
  playlist save/load, host seek/pause/skip, track/subtitle selection, readiness,
  reconnect state, movie gain and per-user mute/gain. Provide controller pointing
  and text entry suitable for the first VR test.
- Package the Rust voice extensions and Opus runtime for Windows as well as Linux;
  descriptor entries are not proof of a working Windows build. Resolve stale
  minimum-version declarations to the actual 4.7 API requirement.
- Run the same friend scenario flat and in PCVR. Only then expand to 4–8 real
  clients and targeted ~16-participant contention tests where they answer an
  observed problem. Retain the existing synthetic tests as regression controls.

**Done:** a real two-person anime/watch session on exported builds with video,
subs, spatial voice, queue controls and visible avatars, followed by a PCVR
session on both OSes. Record the supported codecs/sources and measured limits.

## Immediate next task

Implement phase 1, preserving the current Windows reference and custom mpv SHA.
The first checkpoint is a Linux desktop demo playing the L/R test clip through
the Godot-owned Vulkan texture and Steam Audio streams, alongside a reproducible
Windows build. That checkpoint determines whether the next effort is routine
playback completion or deeper renderer/audio work.

Mechanical porting, dependency packaging, state-machine tests and queue/UI code
are well suited to iterative agent work. The GPU handoff, physical audio clock,
cross-platform exports and headset behavior still require direct measurements;
stronger code generation does not remove those acceptance gates.
