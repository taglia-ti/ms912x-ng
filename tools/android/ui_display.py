#!/usr/bin/env python3
"""Read or operate only MS912x app windows on one display, without restarting the app."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import tempfile
import uuid


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk', type=Path, required=True)
    p.add_argument('--serial', required=True)
    p.add_argument('--display', type=int, default=0)
    p.add_argument('--report', type=Path)
    p.add_argument('action', choices=['dump', 'click', 'scroll'])
    p.add_argument('label', nargs='?')
    args = p.parse_args()
    sdk = args.sdk.resolve()
    adb = [str(sdk/'platform-tools/adb'), '-s', args.serial]
    with tempfile.TemporaryDirectory(prefix='ms912x-ui-display-') as directory:
        temp = Path(directory)
        (temp/'dex').mkdir()
        subprocess.run(['javac', '--release', '11', '-cp', str(sdk/'platforms/android-36/android.jar'),
                        '-d', str(temp), str(Path(__file__).with_name('UiDisplayAutomation.java'))], check=True)
        subprocess.run([str(sdk/'build-tools/36.0.0/d8'), '--min-api', '30', '--lib', str(sdk/'platforms/android-36/android.jar'),
                        '--output', str(temp/'dex'), str(temp/'UiDisplayAutomation.class')], check=True)
        remote = '/data/local/tmp/ms912x-ui-'+uuid.uuid4().hex+'.dex'
        try:
            subprocess.run(adb+['push', str(temp/'dex/classes.dex'), remote], check=True, capture_output=True)
            command = 'CLASSPATH='+shlex.quote('/system/framework/uiautomator.jar:'+remote)
            command += ' app_process / UiDisplayAutomation '+shlex.join([str(args.display), args.action]+([args.label] if args.label else []))
            result = subprocess.run(adb+['shell', '-T', command], text=True, capture_output=True, timeout=20)
            if result.stdout:
                print(result.stdout, end='')
            if result.stderr:
                print(result.stderr, end='')
            if args.report and result.returncode == 0:
                args.report.parent.mkdir(parents=True, exist_ok=True)
                args.report.write_text(json.dumps(json.loads(result.stdout), indent=2)+'\n')
            return result.returncode
        finally:
            subprocess.run(adb+['shell', 'rm', '-f', remote], check=True)


if __name__ == '__main__':
    raise SystemExit(main())
