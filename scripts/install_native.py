#!/usr/bin/env python3
"""Install a locally built runtime as a versioned, reversible release.

Never copies game content, user data or settings into the source tree. The existing
AppImage and launch.sh are retained. Updating an existing release changes only the
runtime/current symlink. A custom wrapper without the known runtime entry is refused.
"""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import uuid


def install(source, root, extra_libs=(), *, activate=True):
    launch = root / "launch.sh"
    old = launch.read_text()
    original_entry = '"$bb_root/runtime/AppRun"'
    native_entry = '"$bb_root/runtime/current/launch.sh"'
    if original_entry not in old and native_entry not in old:
        raise ValueError("Unrecognized launch.sh: retain your local fixes and switch its runtime entry manually")
    for path in ("out/bb-probe", "out/bb-gpu-capabilities", "out/gpu/libbbgpu.so"):
        if not (source / path).is_file():
            raise ValueError(f"Build first: missing {path}")
    # Keep the bridge, NGX runtime and redistributable notice as one optional bundle.
    bridge = source / "out/libbbport_dlss.so"
    ngx = sorted((source / "out").glob("libnvidia-ngx-dlss.so.*"))
    license_file = source / "out/NVIDIA-DLSS-LICENSE.txt"
    if (bridge.exists() or ngx) and not (bridge.is_file() and ngx and license_file.is_file()):
        raise ValueError("Incomplete DLSS bundle: need bridge, NGX runtime and NVIDIA license")
    releases = root / "runtime/releases"
    releases.mkdir(parents=True, exist_ok=True)
    name = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:8]
    stage = Path(tempfile.mkdtemp(prefix=".install-", dir=releases))
    try:
        (stage / "bin/gpu").mkdir(parents=True)
        for file in ("bb-probe", "bb-gpu-capabilities"):
            shutil.copy2(source / "out" / file, stage / "bin" / file)
        shutil.copy2(source / "out/gpu/libbbgpu.so", stage / "bin/gpu/libbbgpu.so")
        # vk_dlss.cpp discovers this bundle next to /proc/self/exe, not in bin/gpu.
        if bridge.is_file():
            for file in [bridge, *ngx]:
                shutil.copy2(file, stage / "bin" / file.name)
            shutil.copy2(license_file, stage / license_file.name)
        for lib in extra_libs:
            shutil.copy2(lib, stage / "bin/gpu" / lib.name)
        for folder in ("scripts", "launcher", "patches", "tools/fsr4cap"):
            shutil.copytree(source / folder, stage / folder, ignore=shutil.ignore_patterns("__pycache__"))
        for folder in ("fsr4_shaders", "fsr4_411"):
            if (root / "data" / folder).is_dir():
                (stage / folder).symlink_to(root / "data" / folder)
        shutil.copy2(source / "run.sh", stage / "run.sh")
        shutil.copy2(source / "LICENSE", stage / "LICENSE")
        entry = stage / "launch.sh"
        entry.write_text('''#!/usr/bin/env bash
set -euo pipefail
bb_release=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
export BB_PREBUILT=1
export BB_INSTALL_ROOT=$(cd -- "$bb_release/../../.." && pwd -P)
export LD_LIBRARY_PATH="$bb_release/bin/gpu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec python3 "$bb_release/launcher/bbport_launcher.py" "$@"
''')
        entry.chmod(0o755)
        for binary in [stage / "bin/bb-probe", stage / "bin/bb-gpu-capabilities",
                       stage / "bin/gpu/libbbgpu.so", *([stage / "bin" / bridge.name, *[
                           stage / "bin" / f.name for f in ngx]] if bridge.is_file() else [])]:
            check = subprocess.run(["ldd", str(binary)], capture_output=True, text=True,
                env=dict(os.environ, LD_LIBRARY_PATH=str(stage / "bin/gpu")))
            if check.returncode or "not found" in check.stdout:
                raise ValueError(f"Runtime libraries unresolved for {binary}:\n" + check.stdout + check.stderr)
        final = releases / name
        os.replace(stage, final)
        if not activate:
            print(f"Staged (not activated): {final}")
            return final
        activate_release(root, final)
        return final
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def activate_release(root, final):
    """Select a tested release, keeping the previous target and custom wrapper."""
    launch = root / "launch.sh"
    old = launch.read_text()
    original_entry = '\"$bb_root/runtime/AppRun\"'
    native_entry = '\"$bb_root/runtime/current/launch.sh\"'
    if original_entry not in old and native_entry not in old:
        raise ValueError("Unrecognized launch.sh; refusing to change custom entry")
    final = final.resolve()
    if final.parent != (root / "runtime/releases").resolve() or not (final / "launch.sh").is_file():
        raise ValueError("Expected a staged release in runtime/releases")
    name = final.name
    current = root / "runtime/current"
    if current.exists() and not current.is_symlink():
        raise ValueError("runtime/current is not a symlink; refusing to replace it")
    if current.is_symlink() and current.resolve() == final:
        print(f"Already active: {final}")
        return
    prior = os.readlink(current) if current.is_symlink() else None
    if prior:
        previous = root / "runtime/previous"
        temp_previous = root / "runtime" / (".previous-" + uuid.uuid4().hex)
        temp_previous.symlink_to(prior)
        os.replace(temp_previous, previous)
    temp_link = root / "runtime" / (".current-" + uuid.uuid4().hex)
    temp_link.symlink_to(Path("releases") / name)
    os.replace(temp_link, current)
    if original_entry in old:
        # Retain the entire wrapper, including driver/overlay workarounds.
        saved = root / ("launch-before-native-" + name + ".sh")
        shutil.copy2(launch, saved)
        temporary = root / (".launch-" + name)
        temporary.write_text(old.replace(original_entry, native_entry))
        temporary.chmod(launch.stat().st_mode & 0o777)
        os.replace(temporary, launch)
        print(f"Previous launch wrapper: {saved}")
    print(f"Installed: {final}")
    print("Rollback: select runtime/previous (if present), or restore the saved launch wrapper.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.home() / "bloodborne")
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--library", type=Path, action="append", default=[])
    parser.add_argument("--stage-only", action="store_true", help="Build a release without switching current")
    parser.add_argument("--activate", type=Path, help="Switch to an already tested staged release")
    args = parser.parse_args()
    try:
        root = args.root.expanduser().resolve()
        if args.activate:
            activate_release(root, args.activate)
        else:
            install(args.source.resolve(), root, args.library, activate=not args.stage_only)
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
