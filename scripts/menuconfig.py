#!/usr/bin/python3
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
os.chdir(ROOT)

try:
    import kconfiglib
    import sysconfig
except ImportError as exc:
    print("Kconfiglib missing.", file=sys.stderr)
    raise SystemExit(1) from exc

# Debian installs menuconfig.py as a standalone module
paths = [
    "/usr/lib/python3/dist-packages",
    sysconfig.get_paths().get("purelib"),
]

for p in paths:
    if p and os.path.exists(os.path.join(p, "menuconfig.py")):
        sys.path.insert(0, p)
        break

try:
    import menuconfig
except ImportError:
    print(
        "Missing Kconfiglib menuconfig UI.\n"
        "Install with: python3 -m pip install kconfiglib",
        file=sys.stderr,
    )
    raise SystemExit(1)

kconf = kconfiglib.Kconfig("Kconfig")

if os.path.exists(".config"):
    kconf.load_config(".config")

menuconfig.menuconfig(kconf)

kconf.write_config(".config")