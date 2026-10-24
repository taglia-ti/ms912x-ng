#!/usr/bin/env python3
"""Launch the bundled development helper as shell UID 2000 and retain its ADB stream."""
import argparse
import hashlib
from pathlib import Path
import shlex
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--serial', required=True)
p.add_argument('--apk', type=Path, default=ROOT / 'android/app/build/outputs/apk/debug/app-debug.apk')
p.add_argument('--package', default='io.github.ms912x.desktop')
args = p.parse_args()
if not all(c.isalnum() or c in '._' for c in args.package):
    p.error('Invalid application package')
adb = ['adb', '-s', args.serial]
uid = subprocess.check_output(adb + ['shell', 'id', '-u'], text=True).strip()
if uid != '2000':
    p.error('The ADB shell must run as UID 2000; root is not supported')
with zipfile.ZipFile(args.apk) as apk:
    helper = apk.read('assets/helper.jar')
    expected = apk.read('assets/helper.sha256').decode('ascii').strip()
digest = hashlib.sha256(helper).hexdigest()
if digest != expected:
    raise SystemExit('Bundled helper digest mismatch')
with tempfile.TemporaryDirectory(prefix='ms912x-helper-') as directory:
    path = Path(directory) / 'helper.jar'
    path.write_bytes(helper)
    remote = '/data/local/tmp/ms912x-helper-' + digest + '.jar'
    subprocess.run(adb + ['push', str(path), remote], check=True, stdout=subprocess.DEVNULL)
    actual = subprocess.check_output(adb + ['shell', 'sha256sum', remote], text=True).split()[0]
    if actual != digest:
        raise SystemExit('Transferred helper digest mismatch')
    command = ('CLASSPATH=' + shlex.quote(remote) + ' app_process / --nice-name=ms912x-helper io.github.ms912x.helper.HelperMain '
               + '--debug-claim ' + shlex.quote(args.package + '.helper'))
    print('Starting helper; first use Prepare ADB helper in the visible Debug page.', flush=True)
    print('Helper SHA-256: ' + digest, flush=True)
    process = subprocess.Popen(adb + ['shell', '-T', command], stdin=subprocess.PIPE)
    try:
        raise SystemExit(process.wait())
    except KeyboardInterrupt:
        process.stdin.close()
        try:
            process.wait(timeout=15)
        except subprocess.TimeoutExpired:
            process.terminate()
            process.wait(timeout=5)
    finally:
        if process.stdin and not process.stdin.closed:
            process.stdin.close()
        # The JAR is content-addressed and can be reused on the next enrollment.
