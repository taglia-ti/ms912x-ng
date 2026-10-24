#!/usr/bin/env python3
"""Check helper Surface pixels using a separate UID's full-screen geometry fixture."""
import argparse
import hashlib
import json
import subprocess
import sys
import time
import zipfile
from pathlib import Path
sys.dont_write_bytecode = True
from helper_test_support import has_live_helper_mirror

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', required=True)
p.add_argument('--source-id', required=True, type=int)
p.add_argument('--rotations', default='0,1,2,3')
p.add_argument('--exercise-source-changes', action='store_true',
               help='Resize/rotate a dedicated temporary fixture display; never use an existing desktop')
p.add_argument('--apk', type=Path, default=ROOT / 'android/app/build/outputs/apk/debug/app-debug.apk')
p.add_argument('--report', required=True, type=Path)
a = p.parse_args()
if a.source_id <= 0:
    p.error('Use a dedicated secondary fixture display')
if not all(v in ('0', '1', '2', '3') for v in a.rotations.split(',')):
    p.error('Rotations must be comma-separated quarter turns')
a.report.mkdir(parents=True, exist_ok=True)
adb = ['adb', '-s', a.serial]
package = 'io.github.ms912x.desktop'

def shell(*args):
    return subprocess.check_output(adb + ['shell', *args], text=True, timeout=30, stderr=subprocess.STDOUT)

window_help = subprocess.run(adb + ['shell', 'wm', 'help'], text=True, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=30).stdout
old_rotation_command = 'set-user-rotation' in window_help

def rotate(rotation):
    if old_rotation_command:
        shell('wm', 'set-user-rotation', 'lock', '-d', str(a.source_id), str(rotation))
    else:
        shell('wm', 'user-rotation', '-d', str(a.source_id), 'lock', str(rotation))

def launch_fixture():
    shell('am', 'start', '--display', str(a.source_id), '--windowingMode', '1', '-W', '-f', '0x18000000',
          '-n', package + '.test/io.github.ms912x.desktop.ExternalDisplayFixtureActivity',
          '--ez', 'geometry', 'true')

def copy_private(name):
    data = subprocess.check_output(adb + ['exec-out', 'run-as', package, 'cat', 'files/' + name], timeout=30)
    (a.report / name).write_bytes(data)
    return data

helper = None
process = None
with (a.report / 'instrumentation.txt').open('w') as output, (a.report / 'helper.log').open('w') as helper_log:
    try:
        (a.report / 'results.json').write_text(json.dumps({'passed': False, 'state': 'running'}) + '\n')
        (a.report / 'before-displays.txt').write_text(shell('dumpsys', 'display'))
        shell('am', 'force-stop', package)
        shell('run-as', package, 'rm', '-f', 'files/helper-geometry-ready.json',
              'files/helper-geometry.json', 'files/helper-geometry-failure.png', 'files/helper-geometry-step.json')
        process = subprocess.Popen(adb + ['shell', 'am', 'instrument', '-w', '-r',
            '-e', 'helperGeometry', 'true', '-e', 'sourceId', str(a.source_id), '-e', 'rotations', a.rotations,
            '-e', 'sourceChanges', str(a.exercise_source_changes).lower(),
            package + '.test/io.github.ms912x.desktop.CaptureNativeTestRunner'], stdout=output, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 25
        while True:
            try:
                if json.loads(shell('run-as', package, 'cat', 'files/helper-geometry-ready.json'))['ready']:
                    break
            except subprocess.CalledProcessError:
                pass
            if process.poll() is not None or time.monotonic() > deadline:
                raise AssertionError('Instrumentation did not open helper enrollment')
            time.sleep(.25)
        # Some OEM instrumentation startup dismisses the separate test APK's
        # Activity. Launch the known source after that startup has completed.
        launch_fixture()
        helper = subprocess.Popen([sys.executable, str(ROOT / 'tools/android/launch_helper.py'),
            '--serial', a.serial, '--apk', str(a.apk)], stdout=helper_log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 240
        last_step = None
        while process.poll() is None:
            if a.exercise_source_changes:
                try:
                    step = json.loads(shell('run-as', package, 'cat', 'files/helper-geometry-step.json'))
                except subprocess.CalledProcessError:
                    step = None
                if step and step['token'] != last_step:
                    assert step['source_id'] == a.source_id
                    last_step = step['token']
                    if step['step'] == 'resize':
                        shell('wm', 'size', '1600x1200', '-d', str(a.source_id))
                    elif step['step'] == 'rotate':
                        rotate(1)
                    elif step['step'] == 'restore':
                        rotate(0)
                        shell('wm', 'size', 'reset', '-d', str(a.source_id))
                    else:
                        raise AssertionError('Unexpected fixture step')
                    # Keep system taskbars and transition UI outside the known
                    # full-screen pattern used as this test's pixel oracle.
                    launch_fixture()
            assert time.monotonic() < deadline, 'Geometry instrumentation timed out'
            time.sleep(.25)
        output.flush()
        report = json.loads(copy_private('helper-geometry.json'))
        report['apk_sha256'] = hashlib.sha256(a.apk.read_bytes()).hexdigest()
        with zipfile.ZipFile(a.apk) as apk:
            report['helper_sha256'] = apk.read('assets/helper.sha256').decode('ascii').strip()
        report['api'] = int(shell('getprop', 'ro.build.version.sdk').strip())
        report['android_build'] = shell('getprop', 'ro.build.fingerprint').strip()
        text = (a.report / 'instrumentation.txt').read_text()
        report['passed'] = report['passed'] and '1 groups passed; 0 failed' in text and 'INSTRUMENTATION_CODE: -1' in text
        (a.report / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
        if not report['passed']:
            try:
                copy_private('helper-geometry-failure.png')
            except subprocess.CalledProcessError:
                pass
            raise AssertionError('Pixel acceptance failed: ' + report.get('failure', text))
        deadline = time.monotonic() + 5
        while True:
            displays = shell('dumpsys', 'display')
            compositor = shell('dumpsys', 'SurfaceFlinger')
            (a.report / 'after-surfaceflinger.txt').write_text(compositor)
            if 'DisplayDeviceInfo{"ms912x-helper-' not in displays and not has_live_helper_mirror(compositor):
                break
            assert time.monotonic() < deadline, 'Helper mirror remains after test cleanup'
            time.sleep(.2)
        assert 'uniqueId "' + report['source_identity'] + '"' in displays, 'Original source disappeared'
        changes = sum(len(case.get('source_changes', [])) for case in report['cases'])
        report['source_preserved'] = True
        report['mirrors_removed'] = True
        (a.report / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
        print('PASS:', len(report['cases']), 'helper geometry cases;', changes, 'live source transitions', flush=True)
    except BaseException as failure:
        result_path = a.report / 'results.json'
        state = json.loads(result_path.read_text())
        state.update(passed=False, state='failed', host_failure=type(failure).__name__)
        result_path.write_text(json.dumps(state, indent=2) + '\n')
        raise
    finally:
        shell('am', 'force-stop', package)
        for child in (process, helper):
            if child:
                try:
                    child.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    child.terminate()
                    child.wait(timeout=5)
        if a.exercise_source_changes:
            rotate(0)
            shell('wm', 'size', 'reset', '-d', str(a.source_id))
        (a.report / 'after-displays.txt').write_text(shell('dumpsys', 'display'))
