#!/usr/bin/env python3
"""Real shell-helper screen creation without USB or a host-created source.

Requires matching Desktop and instrumentation APKs already installed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
sys.dont_write_bytecode = True
from helper_test_support import has_live_helper_mirror

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', required=True)
p.add_argument('--apk', type=Path, required=True)
p.add_argument('--report', type=Path, required=True)
a = p.parse_args()
a.report.mkdir(parents=True, exist_ok=True)
adb = ['adb', '-s', a.serial]
package = 'io.github.taglia_ti.ms912x_ng.desktop'


def shell(*parts):
    return subprocess.check_output(adb+['shell', *parts], text=True, stderr=subprocess.STDOUT, timeout=30)


helper = process = None
result = {'passed': False, 'serial': a.serial, 'apk_sha256': hashlib.sha256(a.apk.read_bytes()).hexdigest()}
with (a.report/'instrumentation.txt').open('w') as output, (a.report/'helper.txt').open('w') as log:
    try:
        shell('am', 'force-stop', package)
        shell('run-as', package, 'rm', '-f', 'files/created-screen-ready.json', 'files/created-screen-result.json')
        process = subprocess.Popen(adb+['shell', 'am', 'instrument', '-w', '-r', '-e', 'createdScreen', 'true',
            package+'.test/io.github.taglia_ti.ms912x_ng.desktop.CaptureNativeTestRunner'], stdout=output, stderr=subprocess.STDOUT)
        deadline = time.monotonic()+25
        while True:
            try:
                if json.loads(shell('run-as', package, 'cat', 'files/created-screen-ready.json'))['ready']:
                    break
            except (subprocess.CalledProcessError, json.JSONDecodeError):
                pass
            assert process.poll() is None and time.monotonic()<deadline, 'Enrollment did not open'
            time.sleep(.2)
        helper = subprocess.Popen([sys.executable, str(ROOT/'tools/android/launch_helper.py'),
            '--serial', a.serial, '--apk', str(a.apk)], stdout=log, stderr=subprocess.STDOUT)
        process.wait(timeout=90)
        helper.wait(timeout=20)
        text = (a.report/'instrumentation.txt').read_text()
        assert '1 groups passed; 0 failed' in text and 'INSTRUMENTATION_CODE: -1' in text, text
        result.update(json.loads(shell('run-as', package, 'cat', 'files/created-screen-result.json')))
        (a.report/'helper-capture.json').write_text(shell('run-as', package, 'cat', 'files/helper-preview.json'))
        assert not has_live_helper_mirror(shell('dumpsys', 'SurfaceFlinger'), 'ms912x-desktop-'), 'Created screen leaked'
    finally:
        if process and process.poll() is None:
            shell('am', 'force-stop', package)
            process.terminate()
        if helper and helper.poll() is None:
            helper.terminate()
        (a.report/'results.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps(result, indent=2))
