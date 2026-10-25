#!/usr/bin/env python3
"""Capture/UI regression for the English 1080x1920 API 30/36 test AVDs."""
import argparse
import os
import tempfile
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial', required=True)
parser.add_argument('--sdk', default=os.environ.get('ANDROID_HOME', os.environ.get('ANDROID_SDK_ROOT')))
parser.add_argument('--apk', type=Path, default=ROOT/'android/app/build/outputs/apk/debug/app-debug.apk')
parser.add_argument('--report', type=Path)
args = parser.parse_args()
if not args.sdk:
    parser.error('Set ANDROID_HOME or pass --sdk')
ADB = [str(Path(args.sdk)/'platform-tools/adb'), '-s', args.serial]
API = int(subprocess.check_output(ADB+['shell','getprop','ro.build.version.sdk'], text=True).strip())
assert API in (30, 36), 'This UI fixture is validated on API 30 and 36'
REPORT = args.report or ROOT/'android/app/build/reports/android-fixtures'/('api'+str(API)+'-ui')
REPORT.mkdir(parents=True, exist_ok=True)
APK = args.apk
results = {'apk_sha256': hashlib.sha256(APK.read_bytes()).hexdigest(), 'checks': []}

def adb(*args):
    return subprocess.check_output(ADB + list(args), text=True, timeout=25)

def save():
    (REPORT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')

def record(name, detail):
    results['checks'].append({'name': name, 'result': 'passed', 'detail': detail})
    save()
    print('PASS:', name, '-', detail, flush=True)

def nodes():
    adb('shell', 'uiautomator', 'dump', '--compressed', '/sdcard/ms912x-retest.xml')
    xml = adb('shell', 'cat', '/sdcard/ms912x-retest.xml')
    (REPORT / 'latest-ui.xml').write_text(xml)
    return list(ET.fromstring(xml).iter('node'))

def matching(all_nodes, label):
    return ([n for n in all_nodes if n.get('content-desc') == label]
            or [n for n in all_nodes if n.get('text') == label]
            or [n for n in all_nodes if n.get('text', '').casefold() == label.casefold()])

def bounds(node):
    return tuple(map(int, re.findall(r'\d+', node.get('bounds'))))

def top():
    for _ in range(3):
        adb('shell', 'input', 'swipe', '540', '500', '540', '1650', '200')

def control(label, click=True):
    all_nodes = nodes()
    for attempt in range(9):
        for node in matching(all_nodes, label):
            x1, y1, x2, y2 = bounds(node)
            # Android 16's taskbar can overlap an edge-to-edge app. Tap only
            # the visible portion; a final collapsed control may not scroll higher.
            visible_top = max(y1, 65)
            visible_bottom = min(y2, 1775 if API >= 36 else 1870)
            if visible_bottom - visible_top >= 24:
                if click:
                    assert node.get('enabled') == 'true', 'Disabled control: ' + label
                    adb('shell', 'input', 'tap', str((x1+x2)//2), str((visible_top+visible_bottom)//2))
                return node
        if attempt == 0:
            top()
        else:
            adb('shell', 'input', 'swipe', '540', '1610', '540', '650', '250')
        all_nodes = nodes()
    raise AssertionError('Control unavailable: ' + label)

def open_debug():
    control('Settings and navigation')
    control('Debug')

def open_main():
    control('Back to screencast')

def screenshot(name):
    data = subprocess.check_output(ADB + ['exec-out', 'screencap', '-p'], timeout=15)
    (REPORT / (name + '.png')).write_bytes(data)
    adb('shell', 'uiautomator', 'dump', '--compressed', '/sdcard/ms912x-retest.xml')
    (REPORT / (name + '.xml')).write_text(adb('shell', 'cat', '/sdcard/ms912x-retest.xml'))

def capture_stats(kind):
    if not matching(nodes(), 'Stop screencast'):
        open_main()
    control('Stop screencast', click=False)
    pattern = (r'Captured (\d+) frames · rejected (\d+)' if kind == 'new'
               else r'accepted native copies: (\d+), rejected: (\d+)')
    if not any(re.search(pattern, node.get('text', '')) for node in nodes()):
        open_debug()
    for _ in range(4):
        for node in nodes():
            match = re.search(pattern, node.get('text', ''))
            if match and int(match[1]) > 0:
                assert int(match[2]) == 0, node.get('text')
                return int(match[1])
        time.sleep(.5)
    raise AssertionError('No accepted ' + kind + ' capture frames')

def display_dump():
    text = adb('shell', 'dumpsys', 'display')
    (REPORT / 'latest-display.txt').write_text(text)
    return text

def stop_capture(name):
    if not matching(nodes(), 'Stop screencast'):
        open_main()
    control('Stop screencast')
    for _ in range(4):
        if name not in display_dump():
            return
        time.sleep(.3)
    raise AssertionError('Display remains after Stop: ' + name)

def start_mirror(approve):
    if not matching(nodes(), 'Test capture without USB output'):
        open_debug()
    control('Test capture without USB output')
    consent = nodes()
    assert any(n.get('package') == 'com.android.systemui' for n in consent), 'Expected Android consent UI'
    labels = ['Start now', 'Share screen', 'Start recording', 'Allow', 'Next'] if approve else ['Cancel', "Don't allow"]
    for label in labels:
        actual = next((n.get('text') for n in consent if n.get('text', '').lower() == label.lower()), None)
        if actual:
            control(actual)
            return
    raise AssertionError('Unknown Android consent buttons: ' + str([n.get('text') for n in consent]))

try:
    subprocess.run(ADB+['install','-r',str(APK)], check=True, timeout=120)
    installed = adb('shell','pm','path','io.github.ms912x.desktop').strip().split('package:')[1]
    with tempfile.TemporaryDirectory() as temporary:
        copy = Path(temporary)/'installed.apk'
        subprocess.run(ADB+['pull',installed,str(copy)], check=True, capture_output=True, timeout=60)
        assert hashlib.sha256(copy.read_bytes()).hexdigest() == results['apk_sha256']
    adb('shell','am','force-stop','io.github.ms912x.desktop')
    adb('logcat','-c')
    adb('shell','am','start','-W','-n','io.github.ms912x.desktop/.MainActivity')
    open_debug()
    control('Clear saved screen and output options')
    open_main()
    results['api'] = adb('shell', 'getprop', 'ro.build.version.sdk').strip()
    assert int(results['api']) == API
    results['android'] = adb('shell', 'getprop', 'ro.build.version.release').strip()
    results['package'] = adb('shell', 'dumpsys', 'package', 'io.github.ms912x.desktop')
    save()
    initial = nodes()
    rotation = matching(initial, 'Screen rotation')[0]
    assert rotation.get('enabled') == ('true' if API >= 36 else 'false')
    record('Android launch and gating', f'API {API}, virtual-display rotation gating matches platform')
    screenshot('01-capture-start')
    control('Source screen')
    choices = [n.get('text') for n in nodes() if n.get('text')]
    assert 'Create a new screen' in choices
    assert 'App test screen (Debug)' not in choices
    assert any('App window' in s for s in choices) == (API >= 34)
    phone = next(s for s in choices if s.startswith('Phone screen'))
    control(phone)
    record('Source dropdown', ', '.join(choices))
    open_debug()
    control('Read monitor modes')
    visible = nodes()
    assert any('No MS912x adapter found' in n.get('text', '') for n in visible)
    open_main()
    assert control('Resolution · EDID ∩ adapter modes', click=False).get('enabled') == 'false'
    record('No-adapter path', 'EDID selectors remain disabled; no fabricated modes')

    control('Source screen')
    control('Create a new screen')
    control('Start screencast')
    assert any(n.get('text') == 'Set up your HDMI screen' for n in nodes())
    control('Open Wireless debugging settings', click=False)
    control('Close')
    assert not re.search(r'DisplayDeviceInfo\{"ms912x-desktop-', display_dump())
    record('New-screen setup guidance', 'Start opens actionable setup steps; closing does not create a desktop')
    control('Source screen')
    control(phone)
    open_debug()
    control('Preview app-owned test screen')
    count = capture_stats('new')
    info = display_dump()
    assert 'ms912x-test-screen' in info and '640 x 480' in info
    assert 'FLAG_OWN_CONTENT_ONLY' in info
    screenshot('02-capture-new-screen')
    (REPORT / 'new-screen-display.txt').write_text(info)
    record('New screen', f'640x480 app-owned test scene; {count} accepted frames, 0 rejected')
    adb('shell', 'input', 'keyevent', '3')
    adb('shell', 'am', 'start', '-W', '-n', 'io.github.ms912x.desktop/.MainActivity')
    resumed = capture_stats('new')
    assert resumed >= count
    stop_capture('ms912x-test-screen')
    record('New-screen background/resume/stop', f'Counter continued to {resumed}; display removed')
    open_debug()
    control('Preview app-owned test screen')
    restarted = capture_stats('new')
    stop_capture('ms912x-test-screen')
    record('New-screen restart', f'Fresh capture accepted {restarted} frames; stop removed display')

    control('Source screen')
    control(phone)
    start_mirror(False)
    assert 'ms912x-consented-screen' not in display_dump()
    assert control('Test capture without USB output', click=False).get('enabled') == 'true'
    record('Consent rejection', 'Decline restored controls and created no capture display')
    start_mirror(True)
    count = capture_stats('mirror')
    info = display_dump()
    assert 'ms912x-consented-screen' in info and '640 x 480' in info
    screenshot('03-capture-phone-mirror')
    (REPORT / 'mirror-display.txt').write_text(info)
    record('Phone-screen mirror', f'640x480 default-display capture; {count} accepted, 0 rejected')
    adb('shell', 'input', 'keyevent', '3')
    adb('shell', 'am', 'start', '-W', '-n', 'io.github.ms912x.desktop/.MainActivity')
    resumed = capture_stats('mirror')
    assert resumed >= count
    stop_capture('ms912x-consented-screen')
    record('Mirror background/resume/stop', f'Counter continued to {resumed}; display removed')
    start_mirror(True)
    restarted = capture_stats('mirror')
    stop_capture('ms912x-consented-screen')
    record('Mirror restart', f'Fresh consent session accepted {restarted} frames; display removed')
    if API >= 34:
        control('Source screen')
        app = next(n.get('text') for n in nodes() if n.get('text', '').startswith('App window'))
        control(app)
        start_mirror(True)
        assert any('Choose app to share' in n.get('text', '') for n in nodes()), 'Expected Android app picker'
        control('MS912x Desktop')
        count = capture_stats('mirror')
        assert 'ms912x-consented-screen' in display_dump()
        screenshot('05-app-window')
        stop_capture('ms912x-consented-screen')
        record('App-window picker: this app', f'Android-selected app captured {count} frames with 0 rejected; stop removed display')
        start_mirror(True)
        control('Settings')
        # Android launches the selected app after returning the consent result.
        deadline = time.monotonic() + 10
        while any('Choose app to share' in n.get('text', '') for n in nodes()):
            assert time.monotonic() < deadline, 'App selector did not dismiss'
        time.sleep(.5)
        adb('shell', 'am', 'start', '-W', '-n', 'io.github.ms912x.desktop/.MainActivity')
        count = capture_stats('mirror')
        stop_capture('ms912x-consented-screen')
        record('App-window picker: Settings', f'Third-party app captured {count} frames with 0 rejected; stop removed display')
    services = adb('shell', 'dumpsys', 'activity', 'services', 'io.github.ms912x.desktop')
    (REPORT / 'services-after-stop.txt').write_text(services)
    assert '.ProjectionCaptureService' not in services
    crashes = adb('logcat', '-d', '-b', 'crash')
    (REPORT / 'crashes.txt').write_text(crashes)
    assert 'io.github.ms912x.desktop' not in crashes
    screenshot('04-capture-stopped')
    record('Final cleanup', 'No capture display, no capture service, no application crash')
    results['result'] = 'passed'
    results['limitation'] = 'No USB adapter attached; physical monitor output not tested'
    save()
    print(f'ALL API {API} CHECKS PASSED:', REPORT, flush=True)
except BaseException as failure:
    results['result'] = 'failed'
    results['failure'] = repr(failure)
    save()
    try:
        screenshot('failure')
        (REPORT/'failure-crashes.txt').write_text(adb('logcat','-d','-b','crash'))
    except Exception:
        pass
    raise

finally:
    # Preserve failure evidence before removing any active capture/service.
    subprocess.run(ADB + ['shell', 'am', 'force-stop', 'io.github.ms912x.desktop'],
                   timeout=25, check=False, stdout=subprocess.DEVNULL)
