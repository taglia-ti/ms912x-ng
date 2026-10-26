#!/usr/bin/env python3
"""Inspect native mouse topology, probe helper-screen admission, or temporarily place an admitted screen."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import threading
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--sdk', type=Path, default=os.environ.get('ANDROID_HOME', os.environ.get('ANDROID_SDK_ROOT')))
    parser.add_argument('--report', type=Path, help='Save JSON events, including unsupported/excluded results')
    operation = parser.add_mutually_exclusive_group()
    operation.add_argument('--probe-screen', action='store_true', help='Create and release a screen using production helper code')
    operation.add_argument('--probe-mouse', action='store_true', help='Create a temporary screen and verify native virtual-mouse motion')
    operation.add_argument('--screen', type=int, help='Existing adapter screen ID; requires --anchor and --position')
    parser.add_argument('--anchor', type=int, help='Explicit native DeX/main display ID from the inventory')
    parser.add_argument('--position', choices=['left', 'right', 'top', 'bottom'])
    parser.add_argument('--seconds', type=int, default=30, help='Restore the previous arrangement after 1..300 seconds')
    parser.add_argument('--width', type=int, default=1280)
    parser.add_argument('--height', type=int, default=720)
    parser.add_argument('--density', type=int, default=160)
    args = parser.parse_args()
    if args.sdk is None:
        parser.error('Set ANDROID_HOME or pass --sdk')
    sdk = args.sdk.resolve()
    if args.screen is not None:
        if args.screen < 0 or args.anchor is None or args.anchor < 0 or args.position is None:
            parser.error('--screen requires a nonnegative --anchor and --position')
        if args.screen == args.anchor:
            parser.error('--screen and --anchor must differ')
        if not 1 <= args.seconds <= 300:
            parser.error('--seconds must be 1..300')
        command_args = ['place', str(args.screen), str(args.anchor), args.position, str(args.seconds)]
    else:
        if args.anchor is not None or args.position is not None:
            parser.error('--anchor and --position require --screen')
        if not (1 <= args.width <= 3840 and 1 <= args.height <= 3840 and 72 <= args.density <= 640):
            parser.error('Probe dimensions must be 1..3840 and density 72..640')
        mode = 'mouse' if args.probe_mouse else 'probe' if args.probe_screen else 'inspect'
        command_args = [mode, str(args.width), str(args.height), str(args.density)] if mode != 'inspect' else ['inspect']
    adb = [str(sdk / 'platform-tools/adb'), '-s', args.serial]
    uid = subprocess.check_output(adb + ['shell', 'id', '-u'], text=True, timeout=15).strip()
    if uid != '2000':
        parser.error('ADB must run as shell UID 2000; root is not supported')
    env = dict(os.environ, ANDROID_HOME=str(sdk), ANDROID_SDK_ROOT=str(sdk))
    build = subprocess.run([str(ROOT / 'android/gradlew'), '-p', str(ROOT / 'android'), '--offline',
                            '--console=plain', ':helper:helperClassesJar'], env=env,
                           text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if build.returncode:
        raise SystemExit(build.stdout)
    helper = ROOT / 'android/helper/build/generated/helper-classes.jar'
    android = sdk / 'platforms/android-36/android.jar'
    sources = [ROOT / 'tools/android' / name for name in
               ['TopologyPlacement.java', 'TopologyLease.java', 'VirtualMouseCompat.java', 'DisplayTopologyExperiment.java']]
    events = []
    exit_code = 1
    with tempfile.TemporaryDirectory(prefix='ms912x-display-topology-') as directory:
        temporary = Path(directory)
        classes, dex = temporary / 'classes', temporary / 'dex'
        classes.mkdir()
        dex.mkdir()
        subprocess.run(['javac', '--release', '11', '-cp', os.pathsep.join(map(str, [android, helper])),
                        '-d', str(classes), *map(str, sources)], check=True)
        subprocess.run([str(sdk / 'build-tools/36.0.0/d8'), '--min-api', '30', '--lib', str(android),
                        '--output', str(dex), str(helper), *map(str, classes.rglob('*.class'))], check=True)
        jar = temporary / 'topology.jar'
        with zipfile.ZipFile(jar, 'w') as archive:
            for path in sorted(dex.glob('*.dex')):
                archive.write(path, path.name)
        remote = '/data/local/tmp/ms912x-topology-' + uuid.uuid4().hex + '.jar'
        process = None
        try:
            subprocess.run(adb + ['push', str(jar), remote], check=True, stdout=subprocess.DEVNULL, timeout=30)
            command = 'CLASSPATH=' + shlex.quote(remote) + ' app_process / --nice-name=ms912x-topology '
            command += 'io.github.ms912x.helper.DisplayTopologyExperiment ' + shlex.join(command_args)
            process = subprocess.Popen(adb + ['shell', '-T', command], stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            lines = []

            def collect():
                for line in process.stdout:
                    lines.append(line)
                    print(line, end='', flush=True)

            collector = threading.Thread(target=collect, daemon=True)
            collector.start()
            try:
                process.wait(timeout=args.seconds + 30 if args.screen is not None else 30)
            except (KeyboardInterrupt, subprocess.TimeoutExpired):
                # Retain stdin until completion; EOF is the helper's cancellation signal.
                if process.stdin and not process.stdin.closed:
                    process.stdin.close()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=5)
            collector.join(timeout=5)
            output = ''.join(lines)
            for line in output.splitlines():
                try:
                    value = json.loads(line)
                    if isinstance(value, dict) and 'event' in value:
                        events.append(value)
                except json.JSONDecodeError:
                    pass
            exit_code = process.returncode
        finally:
            if process is not None and process.poll() is None:
                process.kill()
                process.wait()
            if process is not None and process.stdin and not process.stdin.closed:
                process.stdin.close()
            subprocess.run(adb + ['shell', 'rm', '-f', remote], check=True, timeout=15)
            if args.report is not None:
                args.report.parent.mkdir(parents=True, exist_ok=True)
                args.report.write_text(json.dumps({'serial': args.serial, 'operation': command_args,
                                                  'exit_code': exit_code, 'events': events}, indent=2) + '\n')
    return exit_code


if __name__ == '__main__':
    raise SystemExit(main())
