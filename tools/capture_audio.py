#!/usr/bin/env python3
"""Capture a test sink and timestamp tone onsets at small-block delivery time."""
import array
import json
import pathlib
import signal
import subprocess
import sys
import time

output = pathlib.Path(sys.argv[1])
capture = subprocess.Popen([
    "parec", f"--device={sys.argv[2]}", "--raw", "--rate=48000", "--channels=2",
    "--format=float32le", "--latency-msec=10", "--process-time-msec=10",
], stdout=subprocess.PIPE, bufsize=0)
signal.signal(signal.SIGTERM, lambda *_: capture.terminate())
events = []
frames_total = 0
pending = b""
try:
    with (output / "audio.f32").open("wb") as audio:
        while block := capture.stdout.read(3840):
            received = time.time()
            pending += block
            complete = len(pending) // 8 * 8  # Stereo float32 frames.
            block, pending = pending[:complete], pending[complete:]
            if not block:
                continue
            audio.write(block)
            samples = array.array("f", block)
            frames = len(samples) // 2
            # The fixture has one pulse per second. Ignore individual sine
            # zero crossings and the short HRTF tail within the same pulse.
            if not events or received - events[-1]["time"] > 0.5:
                for index in range(frames):
                    if max(abs(samples[index * 2]), abs(samples[index * 2 + 1])) > 0.004:
                        events.append({"time": received - (frames - index) / 48000,
                                       "sample": frames_total + index})
                        break
            frames_total += frames
finally:
    capture.terminate()
    capture.wait(timeout=3)
    (output / "audio-events.json").write_text(json.dumps(events, indent=2) + "\n")
