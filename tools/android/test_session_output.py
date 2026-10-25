#!/usr/bin/env python3
"""PC-assisted physical USB/HDMI acceptance; the given source must be test-owned."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
import zipfile
sys.dont_write_bytecode = True
from helper_test_support import has_live_helper_mirror
from check_hdmi_geometry import check

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', required=True)
p.add_argument('--source-id', type=int, required=True)
p.add_argument('--source-width', type=int, required=True)
p.add_argument('--source-height', type=int, required=True)
p.add_argument('--apk', type=Path, default=ROOT/'android/app/build/outputs/apk/debug/app-debug.apk')
p.add_argument('--report', type=Path, required=True)
p.add_argument('--launch-package',help='Select an installed app through the created-screen app picker.')
p.add_argument('--create-screen',action='store_true',help='Create the independent screen through the app helper; no host-owned source is used.')
p.add_argument('--phone-mirror', action='store_true', help='Exercise actual default-display MediaProjection instead of the helper.')
p.add_argument('--embedded-host')
p.add_argument('--embedded-port',type=int)
p.add_argument('--drop-stream',action='store_true')
a = p.parse_args()
if bool(a.embedded_host) != bool(a.embedded_port):
    p.error('Provide both embedded host and port')
if a.drop_stream and not a.embedded_host:
    p.error('Stream-loss acceptance requires embedded bootstrap')
if a.launch_package and (not a.create_screen or not __import__('re').fullmatch(r'[a-zA-Z0-9_.]+',a.launch_package)):
    p.error('App-picker acceptance requires creation and a valid package')
if a.create_screen and (a.source_id != -1 or not a.embedded_host):
    p.error('Independent creation requires --source-id -1 and embedded bootstrap')
if a.phone_mirror and a.source_id != 0:
    p.error('Phone mirroring requires --source-id 0')
a.report.mkdir(parents=True, exist_ok=True)
adb = ['adb', '-s', a.serial]
package = 'io.github.ms912x.desktop'
card = '/dev/v4l/by-id/usb-UltraSemi_USB3_Video_00210623-video-index0'
assert Path(card).exists(), 'UltraSemi capture card is absent'

def shell(*args):
    return subprocess.check_output(adb+['shell', *args], text=True, timeout=30, stderr=subprocess.STDOUT)


def private(name):
    return json.loads(shell('run-as', package, 'cat', 'files/'+name))


def fixture():
    shell('am', 'start', '--display', str(a.source_id), '--windowingMode', '1', '-W', '-f', '0x18000000',
          '-n', package+'.test/io.github.ms912x.desktop.ExternalDisplayFixtureActivity', '--ez', 'geometry', 'true')


helper = process = None
result = {'passed': False, 'serial': a.serial, 'apk_sha256': hashlib.sha256(a.apk.read_bytes()).hexdigest(),
          'source_id': a.source_id, 'source_size': [a.source_width, a.source_height], 'capture_card': card, 'stages': [], 'bootstrap': 'embedded' if a.embedded_host else 'pc-assisted', 'termination': 'stream-eof' if a.drop_stream else 'notification-stop'}
with zipfile.ZipFile(a.apk) as apk:
    result['helper_sha256'] = apk.read('assets/helper.sha256').decode().strip()
(a.report/'results.json').write_text(json.dumps(result, indent=2)+'\n')
with (a.report/'instrumentation.txt').open('w') as output, (a.report/'helper.log').open('w') as log:
    try:
        result['android_build'] = shell('getprop', 'ro.build.fingerprint').strip()
        result['api'] = int(shell('getprop', 'ro.build.version.sdk'))
        shell('pm', 'grant', package, 'android.permission.POST_NOTIFICATIONS')
        result['notification_permission'] = 'Granted for the notification Stop acceptance test.'
        shell('am', 'force-stop', package)
        shell('run-as', package, 'rm', '-f', 'files/session-acceptance.json', 'files/session-acceptance.continue')
        (a.report/'before-displays.txt').write_text(shell('dumpsys', 'display'))
        bootstrap_args = ['-e','bootstrapHost',a.embedded_host,'-e','bootstrapPort',str(a.embedded_port)] if a.embedded_host else []
        if a.launch_package: bootstrap_args += ['-e','launchPackage',a.launch_package]
        if a.drop_stream: bootstrap_args += ['-e','dropStream','true']
        process = subprocess.Popen(adb+['shell', 'am', 'instrument', '-w', '-r', '-e', 'sessionAcceptance', 'true',
            '-e', 'sourceId', str(a.source_id), '-e', 'sourceKind', 'phone' if a.phone_mirror else 'desktop' if a.create_screen else 'helper', *bootstrap_args, package+'.test/io.github.ms912x.desktop.CaptureNativeTestRunner'],
            stdout=output, stderr=subprocess.STDOUT)
        deadline = time.monotonic()+240
        last = None
        created_display_id = None
        consent_granted = False
        source_launched = False
        while process.poll() is None:
            try:
                state = private('session-acceptance.json')
            except (subprocess.CalledProcessError, json.JSONDecodeError):
                state = None
            if state and state['phase'] != last:
                last = state['phase']
                print('Stage:', last, flush=True)
                if a.create_screen and state.get('created_display_id',-1)>=0:
                    created_display_id=state['created_display_id']
                if last == 'prepare_embedded' and not a.create_screen:
                    fixture(); source_launched = True
                elif last == 'prepare_helper':
                    fixture()
                    helper = subprocess.Popen([sys.executable, str(ROOT/'tools/android/launch_helper.py'),
                        '--serial', a.serial, '--apk', str(a.apk)], stdout=log, stderr=subprocess.STDOUT)
                elif last in ('streaming', 'background', 'recreated', 'app_launched'):
                    if a.embedded_host and not source_launched and not a.create_screen:
                        fixture(); source_launched = True
                    (a.report/(last+'-service.txt')).write_text(shell('dumpsys', 'activity', 'services', package))
                    (a.report/(last+'-displays.txt')).write_text(shell('dumpsys', 'display'))
                    if a.phone_mirror:
                        assert '.ProjectionCaptureService' in (a.report/(last+'-service.txt')).read_text(), 'Missing consented projection owner'
                    else:
                        assert has_live_helper_mirror(shell('dumpsys', 'SurfaceFlinger'), 'ms912x-desktop-' if a.create_screen else 'ms912x-helper-'), 'No live helper output during streaming'
                        assert '.ProjectionCaptureService' not in (a.report/(last+'-service.txt')).read_text(), 'Helper must not use MediaProjection foreground ownership'
                    attempts = []
                    for attempt in range(2):
                        video = a.report/(last+('-retry' if attempt else '')+'.mkv')
                        subprocess.run(['ffmpeg', '-y', '-v', 'warning', '-f', 'v4l2', '-input_format', 'yuyv422',
                            '-video_size', '1920x1080', '-framerate', '30', '-i', card, '-t', '4', '-c:v', 'ffv1', str(video)],
                            check=True, timeout=20, stdout=subprocess.DEVNULL, stderr=(a.report/(video.stem+'-ffmpeg.txt')).open('w'))
                        geometry = check(video, a.source_width, a.source_height) if not a.phone_mirror and last!='app_launched' else {
                            'passed': True, 'validation': 'Native capture/USB lifecycle and recorded app output; no geometry oracle for this stage.'}
                        (a.report/(video.stem+'-geometry.json')).write_text(json.dumps(geometry, indent=2)+'\n')
                        attempts.append({'recording': video.name, 'geometry': geometry})
                        if geometry['passed']:
                            break
                    if last=='app_launched':
                        activities=shell('dumpsys','activity','activities')
                        (a.report/'launched-app-activities.txt').write_text(activities)
                        import re
                        block=re.search(r'Display #'+str(created_display_id)+r' .*?(?=Display #|$)',activities,re.S)
                        assert block and a.launch_package in block.group(0), 'Selected app did not appear on the created display'
                    result['stages'].append({'state': state, 'attempts': attempts, 'geometry': geometry})
                    assert geometry['passed'], 'HDMI geometry failed: '+str(a.report/(video.stem+'-geometry.json'))
                    shell('run-as', package, 'touch', 'files/session-acceptance.continue')
            if not state or (a.phone_mirror and last == "projection_consent" and not consent_granted):
                # Only handle the app's explicit USB access prompt; never approve screen capture here.
                shell('uiautomator', 'dump', '--compressed', '/sdcard/ms912x-p3-usb.xml')
                xml = ET.fromstring(shell('cat', '/sdcard/ms912x-p3-usb.xml'))
                if a.phone_mirror and last == 'projection_consent':
                    for node in xml.iter('node'):
                        if node.get('package') == 'com.android.systemui' and node.get('text') in (
                                'Iniciar agora', 'Start now', 'Iniciar transmissão', 'Compartilhar tela', 'Share screen'):
                            import re
                            x1,y1,x2,y2=map(int,re.findall(r'\d+',node.get('bounds')))
                            (a.report/'projection-consent.xml').write_text(ET.tostring(xml, encoding='unicode'))
                            shell('input','tap',str((x1+x2)//2),str((y1+y2)//2))
                            consent_granted = True
                if any('USB' in n.get('text', '') and 'MS912' in n.get('text', '') for n in xml.iter('node')):
                    for node in xml.iter('node'):
                        if node.get('text') in ('OK', 'Allow', 'Permitir') and node.get('enabled') == 'true':
                            import re
                            x1,y1,x2,y2=map(int,re.findall(r'\d+',node.get('bounds')))
                            shell('input','tap',str((x1+x2)//2),str((y1+y2)//2))
            assert time.monotonic()<deadline, 'Session acceptance timeout'
            time.sleep(.25)
        assert process.returncode == 0
        text = (a.report/'instrumentation.txt').read_text()
        assert '1 groups passed; 0 failed' in text and 'INSTRUMENTATION_CODE: -1' in text, text
        assert last == 'passed' or private('session-acceptance.json')['phase'] == 'passed'
        after = shell('dumpsys', 'display')
        (a.report/'after-displays.txt').write_text(after)
        assert not has_live_helper_mirror(shell('dumpsys', 'SurfaceFlinger'), 'ms912x-desktop-' if a.create_screen else 'ms912x-helper-'), 'Owned display leaked'
        if not a.phone_mirror:
            capture = private('helper-preview.json')
            (a.report/'helper-capture.json').write_text(json.dumps(capture, indent=2)+'\n')
            assert capture['source_id'] == a.source_id and capture['accepted_images'] > 0
            assert capture['rejected_images'] == 0 and (a.drop_stream or capture['release_acknowledged'])
            assert capture['encoding_owner'] == 'usb-output' and not capture['diagnostic_mode']
        if not a.create_screen:
            assert f'displayId={a.source_id}' in after, 'Original source disappeared'
        else:
            result['created_display_id']=created_display_id
        if not a.phone_mirror and not a.create_screen:
            fixture()  # Confirm its original owner remains usable after Stop.
        assert subprocess.run(adb+['shell', 'pidof', 'ms912x-helper'], stdout=subprocess.PIPE).returncode != 0, 'Helper process survived normal Stop'
        result['passed'] = True
    finally:
        if helper and helper.poll() is None:
            helper.terminate()
            helper.wait(timeout=20)
        if process and process.poll() is None:
            shell('am', 'force-stop', package)
            process.terminate()
            process.wait(timeout=10)
        (a.report/'results.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result, indent=2))
