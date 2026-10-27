#!/usr/bin/env python3
"""Test shell-helper cursor loading, hotspot placement and fallback composition."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import uuid
import zipfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk', type=Path, required=True)
parser.add_argument('--serial', required=True)
parser.add_argument('--require-system', action='store_true')
args = parser.parse_args()
sdk = args.sdk.resolve()
adb = [str(sdk / 'platform-tools/adb'), '-s', args.serial]
helper = root / 'android/helper/build/generated/helper-classes.jar'
android = sdk / 'platforms/android-36/android.jar'
with tempfile.TemporaryDirectory(prefix='ms912x-pointer-test-') as directory:
    temp = Path(directory)
    classes, dex = temp / 'classes', temp / 'dex'
    classes.mkdir()
    dex.mkdir()
    subprocess.run(['javac', '--release', '11', '-cp', os.pathsep.join(map(str, [android, helper])),
                    '-d', str(classes), str(root / 'tools/android/PointerIconHarness.java')], check=True)
    subprocess.run([str(sdk / 'build-tools/36.0.0/d8'), '--min-api', '30', '--lib', str(android),
                    '--output', str(dex), str(helper), *map(str, classes.rglob('*.class'))], check=True)
    archive = temp / 'pointer-test.jar'
    with zipfile.ZipFile(archive, 'w') as jar:
        jar.write(dex / 'classes.dex', 'classes.dex')
    remote = '/data/local/tmp/ms912x-pointer-test-' + uuid.uuid4().hex + '.jar'
    try:
        subprocess.run(adb + ['push', str(archive), remote], check=True, stdout=subprocess.DEVNULL, timeout=30)
        command = 'CLASSPATH=' + shlex.quote(remote) + ' app_process / io.github.taglia_ti.ms912x_ng.helper.PointerIconHarness'
        result = subprocess.run(adb + ['shell', '-T', command], text=True, capture_output=True, timeout=40)
        print(result.stdout + result.stderr, end='')
        if result.returncode or 'PASS: cursor bitmap' not in result.stdout:
            raise SystemExit('Pointer fixture failed')
        if args.require_system and 'SYSTEM_ICON ' not in result.stdout:
            raise SystemExit('System pointer icon could not be loaded')
    finally:
        subprocess.run(adb + ['shell', 'rm', '-f', remote], check=True, timeout=15)
