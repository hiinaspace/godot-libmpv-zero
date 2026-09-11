# godot-libmpv-zero

See the [September 2026 dependency reassessment and Linux/Windows theater plan](REASSESSMENT-2026-09.md)
for current direction. [Linux development instructions](LINUX.md) cover the
patched Godot build, pinned dependencies, and automated Steam Audio playback
checks. Linux desktop playback, a ten-minute soak and scoped stress checks pass;
PCVR validation and release packaging remain in progress. The Windows instructions
below describe the earlier prototype and need revalidation against the current patches.

Experimental Godot 4 GDExtension for `libmpv`-backed video playback with:

- Vulkan external-image video rendering
- consumer-driven audio output from patched `libmpv`
- per-channel Godot `AudioStream` playback
- scene-friendly `MPVPlayer` + `MPVTexture` API
- VR/OpenXR sample scenes
- Steam Audio validation in the sample project
- pause, seek and reload controls with automated playback checks

## Repository layout

- [src/](src): GDExtension source
- [project/](project): sample Godot project used for local validation
- [project/bin/libmpv_zero.gdextension](project/bin/libmpv_zero.gdextension): extension descriptor
- [PLAN.md](PLAN.md): project plan and implementation notes
- [CHARTER.md](CHARTER.md): project goals and constraints

## Windows local build

These helpers describe the earlier prototype. Existing CI dependency artifacts
do not include the current changes under `patches/`. A current Windows build
needs matching patched mpv, libplacebo, Godot and Steam Audio binaries, followed
by Windows runtime validation and updated artifact pins.

Requirements:

- Visual Studio 2022 with MSVC toolchain
- CMake
- `gh` authenticated if you want to pull CI-built `mpv` artifacts
- `godot-cpp` submodule initialized

The historical artifact-staging command is:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\setup_windows_mpv_dev.ps1
```

By default, this stages the latest successful `master` run from the forked `mpv clang` workflow in `hiinaspace/mpv-winbuild-cmake`. You can still override the run explicitly with `-RunId`.

Or stage a local MSYS2 build of the forked `mpv` tree:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\build_mpv_local.ps1
```

Build the extension:

```powershell
& "C:\Program Files\CMake\bin\cmake.exe" -S . -B build-phase0 -DMPV_DIR="$PWD\dependencies\mpv-dev"
& "C:\Program Files\CMake\bin\cmake.exe" --build build-phase0 --config Debug
```

The built DLL and staged runtime land under [project/bin/windows](project/bin/windows).

## Sample run

Console runner:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_sample_console.ps1 -ShowFiltered
```

Use a custom media source:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\run_sample_console.ps1 -MediaSource "https://example.com/video.mp4" -ShowFiltered
```

## Basic usage

Create an `MPVTexture` resource, assign it to your material like any other `Texture2D`, and point an `MPVPlayer` at the same resource:

```gdscript
var video_texture := MPVTexture.new()
var player := MPVPlayer.new()
player.output_texture = video_texture
player.source = "res://movie.mp4"
player.autoplay = true
player.left_audio_target = player.get_path_to($LeftSpeaker)
player.right_audio_target = player.get_path_to($RightSpeaker)
add_child(player)

$ScreenMesh.material_override.albedo_texture = video_texture
```

Runtime control:

```gdscript
player.load("https://example.com/video.mp4")
player.play()
player.pause()
player.seek(12.5)
player.stop()
```

`MPVPlayer` can also drive existing scene audio nodes directly through `left_audio_target` and `right_audio_target`.

Supported targets in the current prototype:

- `AudioStreamPlayer`
- `AudioStreamPlayer3D`
- `SteamAudioPlayer`

## GitHub Actions

- [windows-addon.yml](.github/workflows/windows-addon.yml) builds the Windows addon against staged `mpv` artifacts from the forked `mpv-winbuild-cmake` workflow.
- The workflow artifact is intended to be a reusable addon bundle, not just the raw sample project output.
- Pushing a tag matching `v*` publishes a prerelease automatically.
- You can also dispatch the workflow manually and pass `release_tag` to publish an ad hoc prerelease from the Actions UI.

## Status

Earlier Windows prototype milestones (not revalidated with the current patches):

- stable Vulkan video playback in Godot scenes
- `MPVTexture` resource workflow in authored materials
- stable audio playback without xruns on tested media
- VR sample playback through OpenXR
- Steam Audio spatialization with per-channel playback
- seek and reload transitions tuned to avoid noticeable VR frame hitches on the tested machine

Known rough edges:

- Current Linux/Windows portable release bundles are not validated yet
- Physical A/V sync and headset playback still need manual validation; see [Linux notes](LINUX.md)
- startup / first-frame bring-up is still noticeable

Use the pinned Linux development environment for current local testing. PCVR
validation and portable builds for external testers remain separate steps.
