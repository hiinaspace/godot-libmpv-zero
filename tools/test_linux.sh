#!/usr/bin/env bash
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_dir"
test_name=${1:-playback_smoke}
case "$test_name" in
  playback_smoke|transport|consumer_stall|playback_timing|playback_soak) ;;
  *) echo "Unknown test: $test_name" >&2; exit 2 ;;
esac
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
python3 tools/capture_audio.py "$output_dir" "$sink_name.monitor" > "$output_dir/capture.log" 2>&1 &
capture_pid=$!
runner=(timeout --kill-after=5s "${LIBMPV_ZERO_TEST_TIMEOUT:-90s}")
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
  --render-thread "${LIBMPV_ZERO_RENDER_THREAD:-safe}" --disable-vsync --max-fps "${LIBMPV_ZERO_MAX_FPS:-60}" --resolution 960x640 --verbose --xr-mode off \
  --script "res://tests/$test_name.gd" -- --flat > "$output_dir/godot.log" 2>&1 || status=$?
kill "$capture_pid" 2>/dev/null || true
capture_status=0
wait "$capture_pid" 2>/dev/null || capture_status=$?
capture_pid=
if ((status != 0)); then
  tail -40 "$output_dir/godot.log"
fi
python3 - "$output_dir" "$status" "$capture_status" "$test_name" <<'PY'
import array, json, math, pathlib, re, statistics, sys
output = pathlib.Path(sys.argv[1])
log = (output / 'godot.log').read_text()
samples = array.array('f', (output / 'audio.f32').read_bytes() if (output / 'audio.f32').exists() else b'')
peaks = [max((abs(s) for s in samples[ch::2]), default=0) for ch in range(2)]
errors = re.findall(r'^.*(?:ERROR:|Validation Error:|were leaked|instances were leaked|not being called or stuck|Audio/Video desynchronisation detected).*$', log, re.M)
passed = (int(sys.argv[2]) == 0 and int(sys.argv[3]) == 0 and 'SMOKE_RESULT PASS' in log and not errors and
          all(math.isfinite(s) for s in samples) and all(0.0001 < p <= 1 for p in peaks))
report = {'test': sys.argv[4], 'passed': passed, 'audio_peak_per_channel': peaks,
          'godot_exit_status': int(sys.argv[2]), 'capture_exit_status': int(sys.argv[3]),
          'capture_seconds': len(samples) / 96000, 'errors': errors}
summary = re.search(r'^SMOKE_RESULT (?:PASS|FAIL) (\{.*\})$', log, re.M)
if summary:
    report['scenario'] = json.loads(summary.group(1))
flashes = [json.loads(line.removeprefix('AV_FLASH ')) for line in log.splitlines() if line.startswith('AV_FLASH ')]
if flashes:
    audio_events = json.loads((output / 'audio-events.json').read_text())
    # Exclude startup; this is a software loopback estimate, not measurement
    # of the physical display/headset or DAC. Delivery adds capture latency.
    offsets = [v['time'] - min(audio_events, key=lambda a: abs(a['time'] - v['time']))['time']
               for v in flashes[2:]] if audio_events else []
    report['av_estimate'] = {'matched_flashes': len(offsets),
        'median_video_minus_audio_ms': statistics.median(offsets) * 1000 if offsets else None,
        'max_absolute_ms': max(map(abs, offsets), default=0) * 1000}
    passed = passed and len(offsets) >= 5 and all(abs(offset) < 0.15 for offset in offsets)
    report['passed'] = passed
(output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
print(f'Artifacts: {output}')
sys.exit(0 if passed else 1)
PY
