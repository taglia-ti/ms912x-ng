#!/usr/bin/env python3
"""Check an unscaled, unrotated HDMI recording of the app-owned test scene.

This examines capture-card samples, not fresh Android frames or display FPS.
It decodes an existing recording and never opens a camera or capture device.
Requires FFmpeg and NumPy on the host.
"""

import argparse
import json
from pathlib import Path
import subprocess

import numpy as np


def check(video, skip_frames=0):
    metadata = json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=width,height,r_frame_rate", "-of", "json", str(video)
    ], text=True))
    stream = metadata["streams"][0]
    width, height = stream["width"], stream["height"]
    if not (320 <= width <= 4096 and 240 <= height <= 4096):
        raise ValueError("Unsupported capture size for the test-scene fixture")
    frame_bytes = width * height * 3

    process = subprocess.Popen([
        "ffmpeg", "-v", "error", "-i", str(video), "-map", "0:v:0",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "-"
    ], stdout=subprocess.PIPE)
    count = decoded_count = marker_errors = bar_errors = 0
    reference = None
    states = []
    examples = []
    maximum_changed = 0.0
    try:
        while True:
            data = process.stdout.read(frame_bytes)
            if not data:
                break
            if len(data) != frame_bytes:
                raise ValueError("Truncated decoded frame")
            decoded_count += 1
            if decoded_count <= skip_frames:
                continue
            frame = np.frombuffer(data, dtype=np.uint8).reshape(height, width, 3)
            row = frame[int(height * .884)]
            cyan = (row[:, 0] < 40) & (row[:, 1] > 180) & (row[:, 2] > 170)
            edges = np.diff(np.r_[False, cyan, False].astype(np.int8))
            runs = list(zip(np.flatnonzero(edges == 1).tolist(),
                            np.flatnonzero(edges == -1).tolist()))
            # Border/chroma filtering clips the marker at its leftmost position.
            valid_marker = len(runs) == 1 and (
                int(width / 9 - (11 if runs[0][0] < 8 else 6))
                <= runs[0][1] - runs[0][0] <= int(width / 9 + 5))
            if not valid_marker:
                marker_errors += 1
                if len(examples) < 10:
                    examples.append({"sample": decoded_count - 1, "marker_runs": runs})
            else:
                state = round(runs[0][0] / (width / 8))
                if not states or states[-1] != state:
                    states.append(state)

            bars = frame[int(height * .52):int(height * .73),
                         width // 64:width - width // 64].astype(np.int16)
            if reference is None:
                reference = bars.copy()
            changed = float((np.max(np.abs(bars - reference), axis=2) > 10).mean())
            maximum_changed = max(maximum_changed, changed)
            if changed > .001:
                bar_errors += 1
            count += 1
        if process.wait() != 0:
            raise ValueError("FFmpeg decode failed")
    finally:
        process.stdout.close()
        if process.poll() is None:
            process.kill()
            process.wait()

    return {
        "video": str(video), "frames_analyzed": count,
        "frames_decoded": decoded_count, "startup_frames_excluded": min(skip_frames, decoded_count),
        "dimensions": [width, height], "card_sample_rate": stream["r_frame_rate"],
        "marker_errors": marker_errors, "marker_states": states,
        "static_bar_errors": bar_errors, "examples": examples,
        "maximum_static_bar_changed_fraction": maximum_changed,
        "passed": count > 1 and not marker_errors and not bar_errors and len(states) > 1,
        "scope": "Static bar ROI and one moving marker in the app-owned scene; "
                 "not a whole-frame corruption check or display-FPS measurement."
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("video", type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--skip-frames", type=int, default=0,
                        help="Explicitly exclude N capture-card startup buffers; default: none")
    args = parser.parse_args()
    if args.skip_frames < 0:
        parser.error("--skip-frames must be nonnegative")
    result = check(args.video, args.skip_frames)
    output = json.dumps(result, indent=2) + "\n"
    if args.report:
        args.report.write_text(output)
    print(output, end="")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
