#!/usr/bin/env python3
"""Create local motion, flash/tone, surround-downmix and soft-subtitle fixtures."""
import argparse
import pathlib
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("output", type=pathlib.Path)
parser.add_argument("--seconds", type=int, default=120)
parser.add_argument("--size", default="1280x720")
args = parser.parse_args()
if args.seconds < 1:
    parser.error("--seconds must be positive")
args.output.parent.mkdir(parents=True, exist_ok=True)
subtitles = args.output.with_suffix(".ass")
minutes, seconds = divmod(args.seconds, 60)
hours, minutes = divmod(minutes, 60)
subtitles.write_text(f"""[Script Info]
ScriptType: v4.00+
PlayResX: 1280
PlayResY: 720

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,DejaVu Sans,44,&H0000FFFF,&H000000FF,&H00000000,&H00000000,-1,0,0,0,100,100,0,0,1,3,1,2,20,20,30,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:00.00,{hours}:{minutes:02}:{seconds:02}.00,Default,,0,0,0,,Native libass subtitles - 5.1 center channel test
""")
# The centered marker and center-channel-only tone start together each second.
video = (f"testsrc2=duration={args.seconds}:size={args.size}:rate=30,"
         "drawbox=x=iw/3:y=ih/3:w=iw/3:h=ih/3:color=black:t=fill,"
         "drawbox=x=iw/3:y=ih/3:w=iw/3:h=ih/3:color=white:t=fill:enable='lt(mod(t,1),0.12)'")
audio = ("aevalsrc=exprs='0|0|if(lt(mod(t,1),0.12),0.2*sin(2*PI*1000*t),0)|0|0|0'"
         f":sample_rate=48000:duration={args.seconds}:channel_layout=5.1")
subprocess.run([
    "ffmpeg", "-hide_banner", "-loglevel", "warning", "-y",
    "-filter_threads", "1", "-f", "lavfi", "-i", video,
    "-f", "lavfi", "-i", audio, "-i", str(subtitles),
    "-map", "0:v", "-map", "1:a", "-map", "2:s", "-c:v", "libx264",
    "-threads", "2", "-preset", "fast", "-crf", "23", "-g", "30",
    "-pix_fmt", "yuv420p", "-c:a", "flac", "-c:s", "copy",
    "-disposition:s:0", "default", str(args.output),
], check=True)
print(args.output)
