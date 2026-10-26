#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Run the production presets through udev against isolated synthetic USB devices."""
import argparse
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    for tool in ("bwrap", "udevadm"):
        if not shutil.which(tool):
            parser.error(f"{tool} is required; this test never uses the live sysfs tree")
    repo = Path(__file__).resolve().parent.parent
    rule = repo / "linux/udev/60-ms912x.rules"
    subprocess.run(["udevadm", "verify", str(rule)], check=True)
    with tempfile.TemporaryDirectory(prefix="ms912x-udev-") as temporary:
        root = Path(temporary)
        library = root / "arch.so"
        subprocess.run([*shlex.split(args.cc), "-shared", "-fPIC", "-Wall", "-Wextra",
                        "-Werror", str(repo / "tools/test_udev_arch.c"), "-ldl",
                        "-o", str(library)], check=True)
        sys = root / "sys"
        for bus in ("usb", "pci"):
            (sys / "bus" / bus / "drivers/ms912x").mkdir(parents=True)
        parent = sys / "devices/usb1/1-2"
        device = parent / "1-2:1.0"
        peer = sys / "devices/usb1/1-3/1-3:1.0"
        device.mkdir(parents=True)
        peer.mkdir(parents=True)
        (parent / "uevent").write_text("DEVTYPE=usb_device\n")
        (parent / "subsystem").symlink_to("/sys/bus/usb")
        (device / "driver").symlink_to("/sys/bus/usb/drivers/ms912x")
        names = ("ms912x_damage_scan", "ms912x_damage_scan_stride",
                 "ms912x_damage_scan_tolerance")
        for name in names:
            (peer / name).write_text("peer\n")
        rules = root / "rules"
        rules.mkdir()
        shutil.copyfile(rule, rules / rule.name)
        command = ["bwrap", "--unshare-all", "--die-with-parent", "--ro-bind", "/", "/",
                   "--bind", str(sys), "/sys", "--tmpfs", "/run", "--tmpfs", "/etc/udev",
                   "--tmpfs", "/usr/local", "--ro-bind", str(rules), "/usr/lib/udev/rules.d",
                   "--setenv", "SYSTEMD_DEVICE_VERIFY_SYSFS", "0",
                   "--setenv", "SYSTEMD_LOG_LEVEL", "err",
                   "--setenv", "LD_PRELOAD", str(library)]

        def check(architecture, speed, expected, action="bind", driver="ms912x",
                  devtype="usb_interface", subsystem="usb", present=True, local=False):
            (parent / "speed").write_text(speed + "\n")
            (device / "uevent").write_text(f"DEVTYPE={devtype}\nDRIVER={driver}\n")
            (device / "subsystem").unlink(missing_ok=True)
            (device / "subsystem").symlink_to("/sys/bus/" + subsystem)
            (device / "driver").unlink()
            (sys / "bus" / subsystem / "drivers" / driver).mkdir(exist_ok=True)
            (device / "driver").symlink_to(f"/sys/bus/{subsystem}/drivers/{driver}")
            for name in names:
                (device / name).write_text("inherit\n")
            if not present:
                (device / names[0]).unlink()
            override = rules / "90-ms912x-local.rules"
            override.unlink(missing_ok=True)
            if local:
                override.write_text('ACTION=="bind", DRIVER=="ms912x", '
                                    'ATTR{ms912x_damage_scan_stride}="4"\n')
            result = subprocess.run([*command, "--setenv", "MS912X_TEST_ARCH", architecture,
                                     "udevadm", "test", "--json=short", "--action=" + action,
                                     "/sys/devices/usb1/1-2/1-2:1.0"],
                                    text=True, capture_output=True)
            if result.returncode:
                raise RuntimeError(result.stderr)
            data = json.loads(result.stdout)
            actual = {item["path"]: item["value"] for item in data.get("sysfsAttributes", [])}
            wanted = dict(zip(names, expected)) if expected else {}
            assert actual == wanted, (architecture, speed, action, actual, wanted)
            assert all((peer / name).read_text() == "peer\n" for name in names)

        cases = 0
        for architecture in ("x86-64", "x86", "arm64", "arm"):
            for speed in ("1.5", "12", "480", "5000", "10000", "20000"):
                expected = (("on", "2", "3-15") if speed in ("1.5", "12", "480") else
                            ("auto", "1", "0")) if architecture in ("x86-64", "x86") else ("off", "5", "0")
                check(architecture, speed, expected)
                cases += 1
        for action in ("add", "change", "remove", "unbind"):
            check("x86-64", "480", None, action=action)
            cases += 1
        for options in ({"driver": "other"}, {"devtype": "usb_device"},
                        {"subsystem": "pci"}, {"present": False}):
            check("x86-64", "480", None, **options)
            cases += 1
        check("x86-64", "480", ("on", "4", "3-15"), local=True)
        cases += 1
        print(f"PASS udev: {cases} real rule-engine cases; architecture, negotiated speed, "
              "event guards, local precedence and peer isolation")


if __name__ == "__main__":
    main()
