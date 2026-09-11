#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_dir"
output_dir=${LIBMPV_ZERO_TEST_OUTPUT:-$repo_dir/.local/linux-smoke/$(date +%Y%m%d-%H%M%S)}
mkdir -p "$output_dir"
output_dir=$(cd "$output_dir" && pwd)
sink_name=libmpv_zero_test_$$
module_id=$(pactl load-module module-null-sink sink_name="$sink_name" rate=48000 channels=2)
capture_pid=
cleanup() {
  if [[ -n "$capture_pid" ]]; then
    kill "$capture_pid" 2>/dev/null || true
    wait "$capture_pid" 2>/dev/null || true
  fi
  pactl unload-module "$module_id"
}
trap cleanup EXIT
parec --device="$sink_name.monitor" --raw --rate=48000 --channels=2 --format=float32le > "$output_dir/audio.f32" &
capture_pid=$!
runner=(timeout --kill-after=5s 45s)
if [[ -n "${LIBMPV_ZERO_TEST_CPU_QUOTA:-}" ]]; then
  # Only the game process and its children enter this scope.
  runner=(systemd-run --user --scope --quiet -p "CPUQuota=$LIBMPV_ZERO_TEST_CPU_QUOTA" "${runner[@]}")
fi
export PULSE_SINK="$sink_name"
export LIBMPV_ZERO_MEDIA=${LIBMPV_ZERO_MEDIA:-$repo_dir/project/smoke_test_lr_sync.mp4}
export LIBMPV_ZERO_SCREENSHOT="$output_dir/screen.png"
export VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
export VK_LAYER_VALIDATE_SYNC=1
status=0
"${runner[@]}" "${LIBMPV_ZERO_GODOT:-godot}" --path project --rendering-driver vulkan --display-driver x11 \
  --resolution 960x640 --verbose --xr-mode off --script res://tests/playback_smoke.gd -- --flat > "$output_dir/godot.log" 2>&1 || status=$?
kill "$capture_pid" 2>/dev/null || true
wait "$capture_pid" 2>/dev/null || true
capture_pid=
if ((status != 0)); then
  tail -40 "$output_dir/godot.log"
  exit "$status"
fi
python3 - "$output_dir" <<'PY'
import array, json, math, pathlib, re, sys
output = pathlib.Path(sys.argv[1])
log = (output / 'godot.log').read_text()
samples = array.array('f', (output / 'audio.f32').read_bytes())
peaks = [max((abs(s) for s in samples[ch::2]), default=0) for ch in range(2)]
errors = re.findall(r'^.*(?:ERROR:|Validation Error:|were leaked|instances were leaked).*$', log, re.M)
passed = ('SMOKE_RESULT PASS' in log and not errors and
          all(math.isfinite(s) for s in samples) and all(0.0001 < p <= 1 for p in peaks))
report = {'passed': passed, 'audio_peak_per_channel': peaks,
          'capture_seconds': len(samples) / 96000, 'errors': errors}
(output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
print(f'Artifacts: {output}')
sys.exit(0 if passed else 1)
PY
