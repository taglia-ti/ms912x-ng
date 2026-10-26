#!/usr/bin/env python3
"""PC-assisted acceptance of helper Surface IPC and capture; never releases the source display."""
import argparse
import hashlib
import zipfile
import json
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from pathlib import Path
sys.dont_write_bytecode = True
from helper_test_support import has_live_helper_mirror

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', required=True)
p.add_argument('--source-id', type=int, required=True)
p.add_argument('--runs', type=int, default=2)
p.add_argument('--rotation', type=int, choices=range(4), default=0,
               help='Clockwise helper output rotation in quarter turns')
p.add_argument('--apk', type=Path, default=ROOT / 'android/app/build/outputs/apk/debug/app-debug.apk')
p.add_argument('--report', type=Path, required=True)
args = p.parse_args()
if args.runs < 1:
    p.error('--runs must be positive')
args.report.mkdir(parents=True, exist_ok=True)
ADB = ['adb', '-s', args.serial]
PACKAGE = 'io.github.taglia_ti.ms912x_ng.desktop'
APK_HASH = hashlib.sha256(args.apk.read_bytes()).hexdigest()
with zipfile.ZipFile(args.apk) as packaged:
    HELPER_HASH = packaged.read('assets/helper.sha256').decode('ascii').strip()
(args.report / 'results.json').write_text(json.dumps({'passed': False, 'state': 'running',
    'serial': args.serial, 'apk_sha256': APK_HASH, 'helper_sha256': HELPER_HASH}, indent=2) + '\n')

def adb(*parts, timeout=30):
    return subprocess.check_output(ADB + list(parts), text=True, timeout=timeout, stderr=subprocess.STDOUT)

def inventory():
    return adb('shell', 'dumpsys', 'display')

def source_identity(text):
    for line in text.splitlines():
        if 'mBaseDisplayInfo=DisplayInfo{' in line and re.search(r'displayId ' + str(args.source_id) + r'[, ]', line):
            return re.search(r'uniqueId "([^"]+)"', line).group(1)
    raise AssertionError('Selected source is not present')

def nodes():
    adb('shell', 'uiautomator', 'dump', '--compressed', '/sdcard/ms912x-helper-test.xml')
    data = adb('shell', 'cat', '/sdcard/ms912x-helper-test.xml')
    (args.report / 'latest-ui.xml').write_text(data)
    return list(ET.fromstring(data).iter('node'))

size = adb('shell', 'wm', 'size')
width, height = map(int, re.findall(r'(\d+)x(\d+)', size)[-1])

def touch(x, y):
    adb('shell', 'input', '-d', '0', 'tap', str(x), str(y))
    time.sleep(.4)

def swipe(up=True):
    start, end = (.82, .25) if up else (.25, .82)
    adb('shell', 'input', '-d', '0', 'swipe', str(width // 2), str(int(height * start)),
        str(width // 2), str(int(height * end)), '300')
    time.sleep(.25)

def control(label, click=True, contains=False, scroll=True):
    for attempt in range(15 if scroll else 2):
        for node in sorted(nodes(), key=lambda n: n.get('content-desc') != label):
            text = node.get('text', '')
            matches = (label in text if contains else text == label) or node.get('content-desc') == label
            if not matches:
                continue
            x1, y1, x2, y2 = map(int, re.findall(r'\d+', node.get('bounds')))
            top = max(y1, int(height * .04))
            bottom = min(y2, int(height * .93))
            if bottom - top >= 20:
                if click:
                    assert node.get('enabled') == 'true', 'Disabled: ' + label
                    touch((x1 + x2) // 2, (top + bottom) // 2)
                return node
        if scroll:
            swipe()
    raise AssertionError('Unavailable: ' + label)

def wait_status(fragment, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for node in nodes():
            if fragment in node.get('text', ''):
                return node.get('text')
        time.sleep(.5)
    raise AssertionError('Missing status: ' + fragment)

def report_file():
    return json.loads(adb('shell', 'run-as', PACKAGE, 'cat', 'files/helper-preview.json'))

def wait_mirror_removed(name):
    # Framework release is synchronous at our boundary, but compositor teardown
    # and the display-service inventory can settle in subsequent transactions.
    deadline = time.monotonic() + 5
    while True:
        displays = inventory()
        compositor = adb('shell', 'dumpsys', 'SurfaceFlinger')
        (args.report / ('surfaceflinger-' + name + '.txt')).write_text(compositor)
        if ('DisplayDeviceInfo{"ms912x-helper-' not in displays
                and not has_live_helper_mirror(compositor)):
            return displays
        assert time.monotonic() < deadline, 'Helper mirror remains after release'
        time.sleep(.2)

before = inventory()
identity = source_identity(before)
(args.report / 'before-displays.txt').write_text(before)
process = None
log = (args.report / 'helper.log').open('w')
reports = []
try:
    adb('shell', 'am', 'force-stop', PACKAGE)
    adb('shell', 'am', 'start', '--display', '0', '-W', '-n', PACKAGE + '/.MainActivity', '-f', '0x10200000')
    time.sleep(1)
    control('Source screen')
    if args.source_id == 0:
        control('Phone screen', contains=True, scroll=False)
    else:
        control('(#' + str(args.source_id) + ')', contains=True, scroll=False)
    control('Settings and navigation')
    control('Debug')
    control('Prepare ADB helper')
    process = subprocess.Popen([sys.executable, str(ROOT / 'tools/android/launch_helper.py'),
        '--serial', args.serial, '--apk', str(args.apk)], stdout=log, stderr=subprocess.STDOUT)
    wait_status('Helper connected')
    if args.rotation:
        control('Helper output rotation')
        control(str(args.rotation * 90) + '°', scroll=False)
    for run in range(args.runs):
        adb('shell', 'run-as', PACKAGE, 'rm', '-f', 'files/helper-preview.json')
        control('Preview selected display with ADB helper')
        status = wait_status('Helper preview active')
        print(status, flush=True)
        time.sleep(5)
        control('Stop helper preview')
        wait_status('Helper mirror released')
        deadline = time.monotonic() + 5
        while True:
            try:
                data = report_file()
                break
            except Exception:
                if time.monotonic() > deadline:
                    raise
                time.sleep(.2)
        assert data['source_id'] == args.source_id and data['source_identity'] == identity
        assert data['accepted_images'] > 0 and data['encoded_batches'] > 0
        assert data['changed_payload_signatures'] > 1, 'Source produced no changing encoded pixels'
        assert data['release_acknowledged'], 'Mirror release was not acknowledged'
        assert data['output_rotation'] == args.rotation, 'Wrong helper output rotation'
        after = wait_mirror_removed(str(run))
        assert source_identity(after) == identity, 'Source identity changed during capture'
        reports.append(data)
        (args.report / ('capture-' + str(run) + '.json')).write_text(json.dumps(data, indent=2) + '\n')
        print('PASS: capture/stop/source-preservation', run, data, flush=True)
    # Exercise producer death while a new capture is active.
    adb('shell', 'run-as', PACKAGE, 'rm', '-f', 'files/helper-preview.json')
    control('Preview selected display with ADB helper')
    wait_status('Helper preview active')
    time.sleep(2)
    pids = adb('shell', 'pidof', 'ms912x-helper').split()
    assert len(pids) == 1, 'Expected one enrolled helper process'
    adb('shell', 'kill', '-KILL', pids[0])
    wait_status('Helper disconnected')
    assert source_identity(inventory()) == identity
    deadline = time.monotonic() + 5
    while True:
        try:
            crashed = report_file()
            break
        except Exception:
            if time.monotonic() > deadline:
                raise
            time.sleep(.2)
    assert crashed['accepted_images'] > 0 and not crashed['release_acknowledged']
    assert source_identity(wait_mirror_removed('helper-death')) == identity
    (args.report / 'crash-capture.json').write_text(json.dumps(crashed, indent=2) + '\n')
    (args.report / 'results.json').write_text(json.dumps({'passed': True, 'serial': args.serial,
        'source_identity': identity, 'apk_sha256': APK_HASH, 'helper_sha256': HELPER_HASH,
        'api': int(adb('shell', 'getprop', 'ro.build.version.sdk').strip()),
        'android_build': adb('shell', 'getprop', 'ro.build.fingerprint').strip(), 'captures': reports, 'helper_death': 'passed',
        'limitation': 'PC-assisted capture-only preview; no USB/HDMI or standalone pairing claim'}, indent=2) + '\n')
    print('PASS: helper death and preserved source', flush=True)
except BaseException as failure:
    (args.report / 'results.json').write_text(json.dumps({'passed': False, 'state': 'failed',
        'failure': type(failure).__name__, 'serial': args.serial,
        'apk_sha256': APK_HASH, 'helper_sha256': HELPER_HASH}, indent=2) + '\n')
    raise
finally:
    adb('shell', 'am', 'force-stop', PACKAGE)
    if process:
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=5)
    log.close()
    (args.report / 'after-displays.txt').write_text(inventory())
