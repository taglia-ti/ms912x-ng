#!/usr/bin/env python3
"""QEMU relative mouse -> Android evdev -> production router -> separate-UID app receivers.

Requires matching desktop/test APKs installed and an emulator booted with:
  -qemu -qmp unix:/tmp/ms912x-router-qmp.sock,server=on,wait=off
        -device qemu-xhci,id=ms912x-test-usb
        -device usb-mouse,id=ms912x-test-mouse,bus=ms912x-test-usb.0
Native DeX cursor endpoints are verified separately on the phone.
"""
import argparse
import json
import os
from pathlib import Path
import queue
import re
import shlex
import socket
import subprocess
import tempfile
import threading
import time
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--serial', required=True)
    p.add_argument('--sdk', type=Path, required=True)
    p.add_argument('--qmp', required=True)
    p.add_argument('--report', type=Path, required=True)
    args = p.parse_args()
    args.report.mkdir(parents=True, exist_ok=True)
    sdk = args.sdk.resolve()
    adb = [str(sdk/'platform-tools/adb'), '-s', args.serial]
    env = dict(os.environ, ANDROID_HOME=str(sdk), ANDROID_SDK_ROOT=str(sdk))
    subprocess.run([str(ROOT/'android/gradlew'), '-p', str(ROOT/'android'), '--offline',
                    '--console=plain', ':helper:assembleDexJar'], env=env, check=True, stdout=subprocess.DEVNULL)
    client = socket.socket(socket.AF_UNIX)
    client.settimeout(5)
    client.connect(args.qmp)
    protocol = client.makefile('rwb', buffering=0)
    protocol.readline()

    def qmp(command, arguments=None):
        message = {'execute': command}
        if arguments:
            message['arguments'] = arguments
        protocol.write((json.dumps(message)+'\n').encode())
        while True:
            response = json.loads(protocol.readline())
            if 'error' in response:
                raise AssertionError(response)
            if 'return' in response:
                return response['return']

    def motion(x, y):
        qmp('input-send-event', {'events': [{'type': 'rel', 'data': {'axis': 'x', 'value': x}},
                                          {'type': 'rel', 'data': {'axis': 'y', 'value': y}}]})

    def button(down, name='left'):
        qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'down': down, 'button': name}}]})

    def state(display):
        text = subprocess.check_output(adb+['shell', 'am', 'instrument', '-w', '-r', '-e',
                  'mouseInputDisplay', str(display), 'io.github.ms912x.desktop.test/io.github.ms912x.desktop.CaptureNativeTestRunner'],
                  text=True, stderr=subprocess.STDOUT, timeout=20)
        match = re.search(r'MOUSE_STATE:(\{[^\n]*\})', text)
        assert match and 'INSTRUMENTATION_CODE: -1' in text, text
        result = json.loads(match.group(1))
        if result.get('width', 0) <= 0:
            diagnostic = subprocess.check_output(adb+['shell', 'dumpsys', 'activity', 'activities'], text=True)
            (args.report/'activities.txt').write_text(diagnostic)
            raise AssertionError('No receiver on display '+str(display)+': '+str(result))
        samples.append({'display': display, 'state': result})
        return result

    qmp('qmp_capabilities')
    mice = qmp('query-mice')
    mouse = next(item for item in mice if item['name'] == 'QEMU HID Mouse')
    original = next(item['index'] for item in mice if item['current'])
    qmp('human-monitor-command', {'command-line': 'mouse_set '+str(mouse['index'])})
    samples, lines = [], []
    result = {'passed': False, 'serial': args.serial, 'source': mouse, 'samples': samples}
    process = None
    remote = '/data/local/tmp/ms912x-mouse-test-'+uuid.uuid4().hex+'.jar'
    try:
        with tempfile.TemporaryDirectory(prefix='ms912x-qmp-router-') as directory:
            temp = Path(directory)
            classes, dex = temp/'classes', temp/'dex'
            classes.mkdir()
            dex.mkdir()
            android = sdk/'platforms/android-36/android.jar'
            helper = ROOT/'android/helper/build/generated/helper-classes.jar'
            subprocess.run(['javac', '--release', '11', '-cp', os.pathsep.join(map(str, [android, helper])),
                            '-d', str(classes), str(ROOT/'tools/android/MouseRoutingHarness.java')], check=True)
            subprocess.run([str(sdk/'build-tools/36.0.0/d8'), '--min-api', '30', '--lib', str(android),
                            '--output', str(dex), str(helper), *map(str, classes.rglob('*.class'))], check=True)
            jar = temp/'harness.jar'
            with zipfile.ZipFile(jar, 'w') as out:
                out.write(dex/'classes.dex', 'classes.dex')
                with zipfile.ZipFile(ROOT/'android/helper/build/outputs/helper.jar') as source:
                    for name in source.namelist():
                        if name.startswith('native/') and name.endswith('.so'):
                            out.writestr(name, source.read(name))
            subprocess.run(adb+['push', str(jar), remote], check=True, stdout=subprocess.DEVNULL)
            command = 'CLASSPATH='+shlex.quote(remote)+' app_process / io.github.ms912x.helper.MouseRoutingHarness '+shlex.join(['QEMU USB Mouse', '2'])
            process = subprocess.Popen(adb+['shell', '-T', command], stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            messages = queue.Queue()

            def collect():
                for line in process.stdout:
                    lines.append(line)
                    messages.put(line)

            threading.Thread(target=collect, daemon=True).start()

            def wait_for(prefix, timeout=15):
                deadline = time.monotonic()+timeout
                while time.monotonic() < deadline:
                    try:
                        line = messages.get(timeout=.2)
                        if line.startswith(prefix):
                            return line[len(prefix):].strip()
                    except queue.Empty:
                        assert process.poll() is None, ''.join(lines)
                raise AssertionError('Missing '+prefix+'\n'+''.join(lines))

            ids = json.loads(wait_for('READY:'))
            main_id, screen_id = ids['main'], ids['screen']
            result['displays'] = ids
            time.sleep(.3)
            baseline = state(screen_id)
            motion(500, 0)
            time.sleep(.15)
            crossed = state(screen_id)
            assert crossed['hovers'] > baseline['hovers'], crossed
            assert abs(crossed['x']-180) < 2 and abs(crossed['y']-240) < 2, crossed
            button(True)
            time.sleep(1.2)
            motion(20, 5)
            held = state(screen_id)
            assert held['downs'] == crossed['downs']+1 and held['ups'] == crossed['ups'], held
            assert held['buttons'] == 1 and held['eventDownTime'] == held['downTime'], held
            button(False)
            released = state(screen_id)
            assert released['ups'] == held['ups']+1 and released['buttons'] == 0, released
            button(True, 'wheel-up')
            button(False, 'wheel-up')
            wheel = state(screen_id)
            assert wheel['scrolls'] > released['scrolls'] and wheel['vertical'] > 0, wheel
            motion(-500, -5)
            returned = state(main_id)
            assert returned['hovers'] > 0 and 0 <= returned['x'] < 640, returned
            button(True)
            down = state(main_id)
            assert down['buttons'] == 1, down
            process.stdin.write('stop\n')
            process.stdin.flush()
            wait_for('CLOSED')
            stopped = state(main_id)
            assert stopped['ups'] == down['ups']+1 and stopped['buttons'] == 0, stopped
            button(False)
            process.stdin.write('start\n')
            process.stdin.flush()
            wait_for('STARTED')
            motion(500, 0)
            button(True)
            before_unplug = state(screen_id)
            assert before_unplug['buttons'] == 1, before_unplug
            qmp('device_del', {'id': 'ms912x-test-mouse'})
            wait_for('STOPPED:')
            unplugged = state(screen_id)
            assert unplugged['ups'] == before_unplug['ups']+1 and unplugged['buttons'] == 0, unplugged
            result['passed'] = True
            process.stdin.close()
            process.wait(timeout=10)
            assert process.returncode == 0, ''.join(lines)
    finally:
        if process is not None and process.poll() is None:
            process.stdin.close()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.terminate()
                process.wait(timeout=5)
        subprocess.run(adb+['shell', 'rm', '-f', remote], check=True)
        try:
            qmp('human-monitor-command', {'command-line': 'mouse_set '+str(original)})
        except Exception:
            pass
        client.close()
        (args.report/'harness.txt').write_text(''.join(lines))
        (args.report/'results.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
