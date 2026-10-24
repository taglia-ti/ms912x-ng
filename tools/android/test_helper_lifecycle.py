#!/usr/bin/env python3
"""Accept real shell-helper cancellation and process lifetime on a dedicated secondary source."""
import argparse
import hashlib
import json
import re
import signal
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
p.add_argument('--termination', choices=['watchdog', 'eof', 'app-death', 'helper-death'], default='watchdog')
p.add_argument('--apk', type=Path, default=ROOT / 'android/app/build/outputs/apk/debug/app-debug.apk')
p.add_argument('--report', required=True, type=Path)
a = p.parse_args()
if a.source_id <= 0:
    p.error('Use a dedicated secondary fixture display')
a.report.mkdir(parents=True, exist_ok=True)
adb = ['adb', '-s', a.serial]
package = 'io.github.ms912x.desktop'

def shell(*args):
    return subprocess.check_output(adb + ['shell', *args], text=True, timeout=30, stderr=subprocess.STDOUT)

def private(name):
    return json.loads(shell('run-as', package, 'cat', 'files/' + name))

def identity(displays):
    for line in displays.splitlines():
        if 'mBaseDisplayInfo=DisplayInfo{' in line and re.search(r'displayId ' + str(a.source_id) + r'[, ]', line):
            return re.search(r'uniqueId "([^"]+)"', line).group(1)
    raise AssertionError('Original test source is unavailable')

report = {'passed': False, 'state': 'running', 'serial': a.serial, 'termination': a.termination,
          'apk_sha256': hashlib.sha256(a.apk.read_bytes()).hexdigest()}
with zipfile.ZipFile(a.apk) as apk:
    report['helper_sha256'] = apk.read('assets/helper.sha256').decode().strip()
process = helper = None
result_path = a.report / 'results.json'
with (a.report / 'instrumentation.txt').open('w') as output, (a.report / 'helper.log').open('w') as log:
    try:
        result_path.write_text(json.dumps(report, indent=2) + '\n')
        before = shell('dumpsys', 'display')
        source_identity = identity(before)
        (a.report / 'before-displays.txt').write_text(before)
        shell('am', 'force-stop', package)
        shell('run-as', package, 'rm', '-f', 'files/helper-lifecycle-ready.json',
              'files/helper-lifecycle-active.json', 'files/helper-lifecycle.json')
        process = subprocess.Popen(adb + ['shell', 'am', 'instrument', '-w', '-r', '-e', 'helperLifecycle', 'true',
            '-e', 'sourceId', str(a.source_id), '-e', 'termination', a.termination,
            package + '.test/io.github.ms912x.desktop.CaptureNativeTestRunner'], stdout=output, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 25
        while True:
            try:
                if private('helper-lifecycle-ready.json')['ready']:
                    break
            except subprocess.CalledProcessError:
                pass
            assert process.poll() is None and time.monotonic() < deadline, 'Enrollment did not open'
            time.sleep(.1)
        shell('am', 'start', '--display', str(a.source_id), '--windowingMode', '1', '-W', '-f', '0x18000000',
              '-n', package + '.test/io.github.ms912x.desktop.ExternalDisplayFixtureActivity', '--ez', 'geometry', 'true')
        helper = subprocess.Popen([sys.executable, str(ROOT / 'tools/android/launch_helper.py'), '--serial', a.serial,
            '--apk', str(a.apk)], stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 60
        while True:
            try:
                active = private('helper-lifecycle-active.json')
                if active['active']:
                    break
            except subprocess.CalledProcessError:
                pass
            assert process.poll() is None and time.monotonic() < deadline, 'Capture did not become active'
            time.sleep(.1)
        assert active['source_identity'] == source_identity and active['accepted_images'] > 0
        (a.report / 'active.json').write_text(json.dumps(active, indent=2) + '\n')
        active_compositor = shell('dumpsys', 'SurfaceFlinger')
        (a.report / 'active-surfaceflinger.txt').write_text(active_compositor)
        assert has_live_helper_mirror(active_compositor), 'No live mirror in the active capture inventory'
        termination_start = time.monotonic()
        if a.termination == 'eof':
            helper.send_signal(signal.SIGINT)  # Launcher closes this helper's stdin, preserving the app heartbeat.
        elif a.termination == 'app-death':
            shell('am', 'force-stop', package)
        elif a.termination == 'helper-death':
            pids = shell('pidof', 'ms912x-helper').split()
            assert len(pids) == 1, 'Expected exactly one enrolled helper'
            shell('kill', '-KILL', pids[0])
        process.wait(timeout=22)
        helper.wait(timeout=15)
        output.flush()
        if a.termination != 'app-death':
            measured = private('helper-lifecycle.json')
            report.update(measured)
            text = (a.report / 'instrumentation.txt').read_text()
            assert measured['passed'] and '1 groups passed; 0 failed' in text and 'INSTRUMENTATION_CODE: -1' in text, measured
        else:
            report['checks'] = ['active capture before app death', 'helper exited after owner Binder death']
        elapsed = round((time.monotonic() - termination_start) * 1000)
        if a.termination in ('eof', 'app-death'):
            assert elapsed < 7000, 'Helper depended on heartbeat expiry instead of termination'
        deadline = time.monotonic() + 5
        while True:
            after = shell('dumpsys', 'display')
            compositor = shell('dumpsys', 'SurfaceFlinger')
            (a.report / 'after-displays.txt').write_text(after)
            (a.report / 'after-surfaceflinger.txt').write_text(compositor)
            if 'DisplayDeviceInfo{"ms912x-helper-' not in after and not has_live_helper_mirror(compositor):
                break
            assert time.monotonic() < deadline, 'Helper mirror leaked'
            time.sleep(.1)
        assert identity(after) == source_identity, 'Original source was replaced'
        (a.report / 'after-displays.txt').write_text(after)
        (a.report / 'after-surfaceflinger.txt').write_text(compositor)
        report.update(passed=True, state='complete', termination_elapsed_ms=elapsed,
                      source_preserved=True, mirrors_removed=True, source_identity=source_identity,
                      api=int(shell('getprop', 'ro.build.version.sdk').strip()),
                      android_build=shell('getprop', 'ro.build.fingerprint').strip())
        result_path.write_text(json.dumps(report, indent=2) + '\n')
        print('PASS:', a.termination, len(report['checks']), 'checks;', elapsed, 'ms; source preserved', flush=True)
    except BaseException as failure:
        report.update(passed=False, state='failed', host_failure=type(failure).__name__)
        result_path.write_text(json.dumps(report, indent=2) + '\n')
        try:
            (a.report / 'helper-lifecycle.json').write_text(json.dumps(private('helper-lifecycle.json'), indent=2) + '\n')
        except subprocess.CalledProcessError:
            pass
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
