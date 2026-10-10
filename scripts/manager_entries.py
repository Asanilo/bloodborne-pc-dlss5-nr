#!/usr/bin/env python3
"""Export native Linux manager entries using the installation's launch.sh --play."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess


def desktop_quote(value):
    # Desktop Exec escaping differs from shell escaping; '%' introduces field codes.
    value = str(value).replace("%", "%%")
    for old, new in (("\\", "\\\\\\\\"), ('"', '\\"'), ("$", "\\$"), ("`", "\\`")):
        value = value.replace(old, new)
    return '"' + value + '"'


def export(root, output):
    root = root.expanduser().resolve()
    launch = root / "launch.sh"
    if not launch.is_file():
        raise ValueError(f"Missing {launch}")
    if any(c in str(root) for c in "\r\n"):
        raise ValueError("Installation path must not contain line breaks")
    output.mkdir(parents=True, exist_ok=True)
    assets = Path(__file__).resolve().parent.parent / "launcher/assets"
    for name in ("bloodborne-icon.svg", "bloodborne-cover.svg"):
        shutil.copy2(assets / name, output / name)
    if shutil.which("rsvg-convert"):
        for kind in ("icon", "cover"):
            subprocess.run(["rsvg-convert", str(output / f"bloodborne-{kind}.svg"),
                "-o", str(output / f"bloodborne-{kind}.png")], check=True)
    desktop = output / "bloodborne-pc.desktop"
    desktop.write_text("[Desktop Entry]\nType=Application\nName=Bloodborne PC\n"
        "Name[zh_CN]=血源诅咒 PC\nComment=Native Linux port\n"
        f"Exec={desktop_quote(launch)} --play\nPath={root}\n"
        f"Icon={output.resolve() / 'bloodborne-icon.svg'}\nTerminal=false\nCategories=Game;\n")
    desktop.chmod(0o755)
    installer = output / "bloodborne-lutris.json"
    installer.write_text(json.dumps({"name": "Bloodborne PC", "game_slug": "bloodborne-pc",
        "version": "Native Linux", "slug": "bloodborne-pc-native", "runner": "linux",
        "script": {"game": {"exe": str(launch), "args": "--play", "working_dir": str(root)},
                   "system": {"disable_runtime": True}, "installer": []}}, indent=2) + "\n")
    return desktop, installer


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.home() / "bloodborne")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    for path in export(args.root, args.output or args.root / "managers"):
        print(path)
