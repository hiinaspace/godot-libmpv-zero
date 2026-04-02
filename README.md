# godot-libmpv-zero

Experimental Godot 4 GDExtension for `libmpv`-backed video playback with:

- Vulkan external-image video rendering
- forked `libmpv` audio callback support
- per-channel Godot `AudioStream` playback
- VR/OpenXR sample scenes
- Steam Audio validation in the sample project

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

## GitHub Actions

- [windows-addon.yml](/S:/code/godot-libmpv-zero/.github/workflows/windows-addon.yml) builds the Windows addon against staged `mpv` artifacts from the forked `mpv-winbuild-cmake` workflow.
- The workflow artifact is intended to be a reusable addon bundle, not just the raw sample project output.
- Pushing a tag matching `v*` publishes a prerelease automatically.
- You can also dispatch the workflow manually and pass `release_tag` to publish an ad hoc prerelease from the Actions UI.

## Status

Current prototype milestones already validated locally:

- stable Vulkan video playback in Godot
- stable audio playback without xruns on tested media
- VR sample playback through OpenXR
- Steam Audio spatialization with per-channel playback

This is still prototype software. Packaging, API cleanup, and release polish are still in progress.
