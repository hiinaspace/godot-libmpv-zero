# godot-libmpv-zero

Experimental Godot 4 GDExtension for `libmpv`-backed video playback with:

- Vulkan external-image video rendering
- forked `libmpv` audio callback support
- per-channel Godot `AudioStream` playback
- scene-friendly `MPVPlayer` + `MPVTexture` API
- VR/OpenXR sample scenes
- Steam Audio validation in the sample project
- transport operations tuned to avoid noticeable VR frame hitches on tested hardware

## Repository layout

- [src/](/S:/code/godot-libmpv-zero/src): GDExtension source
- [project/](/S:/code/godot-libmpv-zero/project): sample Godot project used for local validation
- [project/bin/libmpv_zero.gdextension](/S:/code/godot-libmpv-zero/project/bin/libmpv_zero.gdextension): extension descriptor
- [PLAN.md](/S:/code/godot-libmpv-zero/PLAN.md): project plan and implementation notes
- [CHARTER.md](/S:/code/godot-libmpv-zero/CHARTER.md): project goals and constraints

## Local build

Requirements:

- Visual Studio 2022 with MSVC toolchain
- CMake
- `gh` authenticated if you want to pull CI-built `mpv` artifacts
- `godot-cpp` submodule initialized

Stage the current Windows `mpv` package from GitHub Actions:

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

The built DLL and staged runtime land under [project/bin/windows](/S:/code/godot-libmpv-zero/project/bin/windows).

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

- [windows-addon.yml](/S:/code/godot-libmpv-zero/.github/workflows/windows-addon.yml) builds the Windows addon against staged `mpv` artifacts from the forked `mpv-winbuild-cmake` workflow.
- The workflow artifact is intended to be a reusable addon bundle, not just the raw sample project output.
- Pushing a tag matching `v*` publishes a prerelease automatically.
- You can also dispatch the workflow manually and pass `release_tag` to publish an ad hoc prerelease from the Actions UI.

## Status

Current prototype milestones already validated locally:

- stable Vulkan video playback in Godot scenes
- `MPVTexture` resource workflow in authored materials
- stable audio playback without xruns on tested media
- VR sample playback through OpenXR
- Steam Audio spatialization with per-channel playback
- seek and reload transitions tuned to avoid noticeable VR frame hitches on the tested machine

Known rough edges:

- Windows x86_64 is the only release-ready target today
- wrapped texture RID cleanup still leaks on exit instead of risking reload-time crashes
- startup / first-frame bring-up is still noticeable

This is still prototype software, but the core playback path is now usable enough for external testing and prerelease builds.
