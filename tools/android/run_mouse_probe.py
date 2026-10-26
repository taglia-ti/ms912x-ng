#!/usr/bin/env python3
"""Run a shell-only phone cursor/calibration/recovery probe from the current helper build."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('mode', choices=['calibrate', 'edge', 'leave', 'recover'])
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    adb = [str(sdk/'platform-tools/adb'), '-s', args.serial]
    assert subprocess.check_output(adb+['shell', 'id', '-u'], text=True).strip() == '2000', 'Shell UID 2000 required'
    env = dict(os.environ, ANDROID_HOME=str(sdk), ANDROID_SDK_ROOT=str(sdk))
    subprocess.run([str(ROOT/'android/gradlew'), '-p', str(ROOT/'android'), '--offline', '--console=plain',
                    ':helper:assembleDexJar'], env=env, check=True, stdout=subprocess.DEVNULL)
    with tempfile.TemporaryDirectory(prefix='ms912x-mouse-probe-') as directory:
        temp = Path(directory)
        classes, dex = temp/'classes', temp/'dex'
        classes.mkdir()
        dex.mkdir()
        android = sdk/'platforms/android-36/android.jar'
        helper = ROOT/'android/helper/build/generated/helper-classes.jar'
        subprocess.run(['javac', '--release', '11', '-cp', os.pathsep.join(map(str, [android, helper])),
                        '-d', str(classes), str(ROOT/'tools/android/MouseRecoveryHarness.java')], check=True)
        subprocess.run([str(sdk/'build-tools/36.0.0/d8'), '--min-api', '30', '--lib', str(android),
                        '--output', str(dex), str(helper), *map(str, classes.rglob('*.class'))], check=True)
        jar = temp/'probe.jar'
        with zipfile.ZipFile(jar, 'w') as out:
            out.write(dex/'classes.dex', 'classes.dex')
            with zipfile.ZipFile(ROOT/'android/helper/build/outputs/helper.jar') as source:
                for name in source.namelist():
                    if name.startswith('native/') and name.endswith('.so'):
                        out.writestr(name, source.read(name))
        remote = '/data/local/tmp/ms912x-mouse-probe-'+uuid.uuid4().hex+'.jar'
        try:
            subprocess.run(adb+['push', str(jar), remote], check=True, stdout=subprocess.DEVNULL, timeout=20)
            command = 'CLASSPATH='+shlex.quote(remote)+' app_process / io.github.ms912x.helper.MouseRecoveryHarness '+args.mode
            result = subprocess.run(adb+['shell', '-T', command], text=True, capture_output=True, timeout=20)
            print(result.stdout+result.stderr, end='')
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(result.stdout+result.stderr)
            return result.returncode
        finally:
            subprocess.run(adb+['shell', 'rm', '-f', remote], check=True, timeout=15)


if __name__ == '__main__':
    raise SystemExit(main())
