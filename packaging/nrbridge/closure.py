#!/usr/bin/env python3
"""Small ELF runtime-closure collector for the NRBridge AppImage preview.

The upstream Nix package lets the linker's store paths define the closure.  The
private preview is built from an already compiled Native baseline, so its
dependencies are ordinary host/sysroot paths.  This module mirrors the same
pruning rule: follow NEEDED, copy only the libraries the processes actually
load, and leave the kernel/glibc/driver boundary to the host.

It deliberately does not use ``ldd`` output as a security boundary; all inputs
are locally built, checked artifacts.  ``ldd`` is used only to resolve the same
dynamic-linker search the programs use.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
from pathlib import Path

# Loaded by the host kernel / dynamic loader.  Bundling any of these makes an
# AppImage fragile across distributions and can break an NVIDIA/AMD driver that
# was built for the host libc.
GLIBC = {
    "ld-linux-x86-64.so.2",
    "ld-linux.so.2",
    "libanl.so.1",
    "libc.so.6",
    "libdl.so.2",
    "libm.so.6",
    "libmvec.so.1",
    "libnsl.so.1",
    "libnss_compat.so.2",
    "libnss_dns.so.2",
    "libnss_files.so.2",
    "libnss_hesiod.so.2",
    "libnss_nis.so.2",
    "libnss_nisplus.so.2",
    "libpthread.so.0",
    "libresolv.so.2",
    "librt.so.1",
    "libutil.so.1",
    "linux-vdso.so.1",
}

# Driver/display stack: the host's kernel module and Vulkan ICD must agree.
# Bundling a loader, GBM, EGL or CUDA from the build host is exactly the class
# of failure the upstream package avoids by taking NVIDIA libraries from the
# host and Mesa's Vulkan drivers from one pinned package.
DRIVER_PREFIXES = (
    "libcuda",
    "libdrm",
    "libEGL",
    "libgbm",
    "libGL.so",
    "libglapi",
    "libGLES",
    "libGLX",
    "libnvcuvid",
    "libnvidia-",
    "libOpenGL",
    "libvdpau",
    "libvulkan",
)

_LDD = re.compile(r"^\s*(\S+)\s+=>\s+(\S+)\s+\(0x[0-9a-fA-F]+\)\s*$")


class ClosureError(RuntimeError):
    pass


def _is_denied(name: str) -> bool:
    base = Path(name).name
    return base in GLIBC or base.startswith(DRIVER_PREFIXES)


def _ldd(path: Path, search: list[Path]) -> list[tuple[str, str]]:
    env = dict(os.environ)
    if search:
        env["LD_LIBRARY_PATH"] = os.pathsep.join(str(p) for p in search)
    run = subprocess.run(
        ["ldd", str(path)], capture_output=True, text=True, env=env, timeout=120
    )
    if run.returncode != 0:
        raise ClosureError(f"ldd failed for {path}: {run.stderr.strip()}")
    found: list[tuple[str, str]] = []
    for line in run.stdout.splitlines():
        match = _LDD.match(line)
        if match:
            name, resolved = match.groups()
            if resolved == "not found":
                raise ClosureError(f"{path}: {name} not found")
            found.append((name, resolved))
            continue
        if "=> not found" in line:
            raise ClosureError(f"{path}: {line.strip()}")
    return found


def _copy(src: Path, dest: Path) -> None:
    if not src.is_file():
        raise ClosureError(f"not a regular library: {src}")
    if dest.exists():
        return
    shutil.copy2(src, dest, follow_symlinks=True)
    dest.chmod(0o755)


def _alias(real: Path, alias: Path) -> None:
    """Give a NEEDED name its own name without duplicating the payload."""
    if alias == real:
        return
    if alias.exists() or alias.is_symlink():
        return
    alias.symlink_to(real.name)


def collect(
    roots: list[Path],
    dest: Path,
    search: list[Path],
    *,
    verbose: bool = False,
) -> dict[str, str]:
    """Copy the transitive runtime closure of *roots* into *dest*.

    Returns a map ``needed name -> source path``.  System/driver libraries are
    left on the host and are not in the map.  Every copied library is scanned
    again with *dest* first on the search path, so libraries that were only
    reachable through a copied dependency are still found.
    """
    dest.mkdir(parents=True, exist_ok=True)
    copied: dict[str, Path] = {}
    queue: list[tuple[Path, list[Path]]] = [(Path(root), list(search)) for root in roots]
    seen_sources: set[Path] = set()
    while queue:
        source, source_search = queue.pop(0)
        source = source.resolve()
        if source in seen_sources or _is_denied(source.name):
            continue
        seen_sources.add(source)
        for needed, resolved in _ldd(source, source_search):
            if _is_denied(needed) or _is_denied(resolved):
                continue
            resolved_path = Path(resolved).resolve()
            alias = dest / Path(needed).name
            real = dest / resolved_path.name
            _copy(resolved_path, real)
            _alias(real, alias)
            copied[Path(needed).name] = resolved_path
            copied.setdefault(resolved_path.name, resolved_path)
            if verbose:
                print(f"  {Path(needed).name} <- {resolved_path}")
            # Scan the copied name: it is what the runtime will open, and its
            # own NEEDED may include another library from the same sysroot.
            queue.append((alias, [dest, *source_search]))
    return {name: str(path) for name, path in sorted(copied.items())}
