# Linux development

The initial prototype uses a patched Godot 4.7.2 build. Stock Godot does not
enable the timeline-semaphore and host-query-reset device features required by
the current shared-device libplacebo path. Revisit stock-engine support after
playback is reliable; the flake keeps an official unpatched build as a control.

## Build and run

On x86_64 Linux with Nix flakes enabled and a working Vulkan desktop driver:

```sh
bash tools/setup_linux.sh
nix develop
LIBMPV_ZERO_MEDIA="$PWD/project/smoke_test_lr_sync.mp4" \
  godot --path project --rendering-driver vulkan --xr-mode off \
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

## Automated playback check

```sh
nix develop --command bash tools/test_linux.sh
LIBMPV_ZERO_TEST_CPU_QUOTA=150% nix develop --command bash tools/test_linux.sh
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

The test omits the scene's ReflectionProbe: Godot 4.7.2 also leaks seven RD
textures when this probe is tested without MPVPlayer. The unmodified demo still
contains the probe. Immediate audio shutdown also has an intermittent object
leak under investigation. Passing short runs establish basic playback only;
audio clock accuracy, sustained playback, subtitle/source coverage, headset
behavior, Windows, and distributable exports require further validation.

The staged Linux libraries are development builds with Nix runtime paths.
They are not yet a portable release bundle. Existing Windows release history
does not validate the current dependency patches on Windows.
