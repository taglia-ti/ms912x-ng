#!/usr/bin/env python3
"""Check decoded HDMI samples of ExternalDisplayFixtureActivity's fit/geometry scene.

Only reads the given recording. Counts are capture-card samples, not display FPS.
"""
import argparse
import json
from pathlib import Path
import subprocess
import numpy as np


def check(video, source_width, source_height, skip_samples=15):
    meta = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
        '-show_entries', 'stream=width,height', '-of', 'json', str(video)], text=True))['streams'][0]
    width, height = meta['width'], meta['height']
    scale = min(width / source_width, height / source_height)
    cw, ch = round(source_width * scale), round(source_height * scale)
    left, top = (width - cw) // 2, (height - ch) // 2
    colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0),
              (255, 0, 0), (0, 255, 0), (0, 0, 255)]
    points = [(.08, .08), (.92, .08), (.92, .92), (.08, .92), (.32, .5), (.5, .5), (.68, .5)]
    process = subprocess.Popen(['ffmpeg', '-v', 'error', '-i', str(video), '-map', '0:v:0',
        '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], stdout=subprocess.PIPE)
    count = decoded = errors = 0
    states, examples, circle_diameters = set(), [], []
    def patch(frame, x, y):
        return np.median(frame[max(0, y-3):y+4, max(0, x-3):x+4], axis=(0, 1))
    try:
        while True:
            raw = process.stdout.read(width * height * 3)
            if not raw:
                break
            if len(raw) != width * height * 3:
                raise ValueError('Truncated decoded sample')
            frame = np.frombuffer(raw, np.uint8).reshape(height, width, 3)
            decoded += 1
            if decoded <= skip_samples:
                continue
            count += 1
            problems = []
            for point, color in zip(points, colors):
                actual = patch(frame, left + round(point[0] * cw), top + round(point[1] * ch))
                if np.max(np.abs(actual - color)) > 60:
                    problems.append({'point': point, 'expected': color, 'actual': actual.tolist()})
            for x, y in ([(width//2, top//2), (width//2, height-1-top//2)] if top > 4 else []) + (
                    [(left//2, height//2), (width-1-left//2, height//2)] if left > 4 else []):
                if patch(frame, x, y).max() > 25:
                    problems.append({'black_bar': [x, y]})
            cx, cy, radius = left + round(.5*cw), top + round(.2*ch), .07*ch
            # Restrict the scan to the circle, excluding the outside frame and color blocks.
            half = round(radius + 24*scale)
            white_x = np.flatnonzero(np.min(frame[cy, cx-half:cx+half+1], axis=1) > 180)
            white_y = np.flatnonzero(np.min(frame[cy-half:cy+half+1, cx], axis=1) > 180)
            if len(white_x) < 2 or len(white_y) < 2:
                problems.append({'circle': 'missing'})
            else:
                dx, dy = int(white_x[-1]-white_x[0]), int(white_y[-1]-white_y[0])
                circle_diameters.append([dx, dy])
                if abs(dx-dy) > 4 or abs(dx-(2*radius+32*scale)) > 5:
                    problems.append({'circle_diameter': [dx, dy]})
            marker = patch(frame, left+round(.5*cw), top+round(.745*ch))
            if marker[0] < 80 and marker[1] > 160 and marker[2] > 160:
                states.add('cyan')
            elif marker[0] > 160 and marker[1] < 80 and marker[2] > 160:
                states.add('magenta')
            else:
                problems.append({'live_marker': marker.tolist()})
            if problems:
                errors += 1
                if len(examples) < 3:
                    examples.append({'sample': count, 'errors': problems})
    finally:
        process.stdout.close()
        result = process.wait(timeout=30)
    return {'passed': result == 0 and count >= 30 and errors == 0 and len(states) == 2,
            'decoded_samples': decoded, 'checked_samples': count, 'excluded_samples': min(decoded, skip_samples),
            'exclusion': 'First 15 samples allow the UVC stream to settle after opening.' if skip_samples == 15 else 'Explicit initial sample exclusion.',
            'samples_with_errors': errors, 'marker_states': sorted(states),
            'content_bounds': [left, top, left+cw, top+ch], 'capture_size': [width, height],
            'circle_diameters': sorted({tuple(v) for v in circle_diameters}), 'examples': examples,
            'limitation': 'Capture-card samples are not display FPS or independent Android frame counts.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('video', type=Path)
    parser.add_argument('--source-width', type=int, required=True)
    parser.add_argument('--source-height', type=int, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    result = check(args.video, args.source_width, args.source_height)
    args.report.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['passed'] else 1)
