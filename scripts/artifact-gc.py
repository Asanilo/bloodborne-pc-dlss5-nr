#!/usr/bin/env python3
"""Reclaim build artifacts with a dry-run by default.

Categories:
  releases   out/releases/<id>/ (immutable release/baseline builds)
  appimages  dist/*.AppImage plus their .sha256 sidecars
  temp       entries under out/tmp older than --min-age-hours
  evidence   optional: entries under out/evidence older than --evidence-max-age-days

Never touched: out/deps, out/cache, out/toolchain, the mutable development build in out/,
registered git worktrees, files open in a process, and the release targets of
/home/arinp/bloodborne/runtime/current|previous.

Examples:
  python3 scripts/artifact-gc.py                    # dry-run, defaults
  python3 scripts/artifact-gc.py --apply            # delete candidates
  python3 scripts/artifact-gc.py --keep-releases 3 --budget-gb 12 --apply
  python3 scripts/artifact-gc.py --json out/gc-report.json --apply
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

SCRIPT_ROOT = Path(__file__).resolve().parent.parent


def main_worktree(start: Path) -> Path:
    """Return the main worktree, so a copy in .worktree/nrbridge or .worktree/fg still
    scans the shared out/releases, dist and cache instead of its own branch checkout."""
    try:
        out = subprocess.run(["git", "-C", str(start), "worktree", "list", "--porcelain"],
                             capture_output=True, text=True, check=True).stdout
        for line in out.splitlines():
            if line.startswith("worktree "):
                return Path(line[9:]).resolve()
    except Exception:
        pass
    return start.resolve()


SHARED_ROOT = Path(os.environ.get("BB_SHARED_ROOT") or main_worktree(SCRIPT_ROOT)).resolve()
RUNTIME_ROOT = Path("/home/arinp/bloodborne/runtime")

DEFAULTS = {
    "release_root": "out/releases",
    "dist_dir": "dist",
    "temp_dir": "out/tmp",
    "keep_releases": 2,
    "keep_appimages": 3,
    "budget_gb": 8.0,
    "min_age_hours": 24,
    "ccache_max_gb": 10.0,
    "evidence_dir": "out/evidence",
    "evidence_max_gb": 2.0,
    "evidence_max_age_days": 180,
}


def human(n: int | float) -> str:
    for unit in ("B", "KiB", "MiB", "GiB", "TiB"):
        if abs(n) < 1024 or unit == "TiB":
            return f"{n:.1f} {unit}" if unit != "B" else f"{int(n)} B"
        n /= 1024
    return f"{n:.1f} TiB"


def tree_size(path: Path) -> int:
    total = 0
    if path.is_symlink() or path.is_file():
        try:
            return path.lstat().st_size
        except OSError:
            return 0
    for root, dirs, files in os.walk(path):
        for name in files:
            try:
                total += (Path(root) / name).lstat().st_size
            except OSError:
                pass
    return total


def age_hours(path: Path) -> float:
    try:
        return (time.time() - path.lstat().st_mtime) / 3600
    except OSError:
        return 0.0


def worktrees() -> list[Path]:
    try:
        out = subprocess.run(["git", "-C", str(SHARED_ROOT), "worktree", "list", "--porcelain"],
                             capture_output=True, text=True, check=True).stdout
    except Exception:
        return []
    return [Path(line[7:]).resolve() for line in out.splitlines() if line.startswith("worktree ")]


def open_paths() -> set[str]:
    paths: set[str] = set()
    for fd in Path("/proc").glob("[0-9]*/fd"):
        try:
            for entry in fd.iterdir():
                try:
                    target = os.readlink(entry)
                except OSError:
                    continue
                if target.startswith("/"):
                    paths.add(target)
        except OSError:
            continue
    return paths


def runtime_protected() -> set[str]:
    names: set[str] = set()
    for link in ("current", "previous"):
        p = RUNTIME_ROOT / link
        try:
            names.add(p.resolve().name)
        except OSError:
            pass
    return names


@dataclass
class Item:
    path: Path
    size: int
    reason: str
    action: str = "delete"
    protected: bool = False
    note: str = ""
    extra: list[Path] = field(default_factory=list)


def protected_reason(path: Path, roots: list[Path], open_set: set[str], min_age_hours: float) -> str:
    resolved = path.resolve()
    for root in roots:
        try:
            resolved.relative_to(root)
            return f"inside worktree {root}"
        except ValueError:
            pass
    if age_hours(path) < min_age_hours:
        return f"modified < {min_age_hours:g}h ago"
    prefix = str(resolved) + os.sep
    for op in open_set:
        if op == str(resolved) or op.startswith(prefix):
            return "open in a running process"
    return ""


def collect(args) -> list[Item]:
    items: list[Item] = []
    release_root = (SHARED_ROOT / args.release_root).resolve()
    dist_dir = (SHARED_ROOT / args.dist_dir).resolve()
    temp_dir = (SHARED_ROOT / args.temp_dir).resolve()
    roots = worktrees()
    open_set = open_paths()
    runtime_names = runtime_protected()

    # Immutable release builds, newest first.
    releases: list[Path] = []
    if release_root.is_dir():
        releases = sorted((p for p in release_root.iterdir() if p.is_dir()),
                          key=lambda p: p.lstat().st_mtime, reverse=True)
    kept_releases: set[Path] = set()
    for i, p in enumerate(releases):
        if i < args.keep_releases or p.name in runtime_names:
            kept_releases.add(p)
            continue
        reason = protected_reason(p, roots, open_set, args.min_age_hours)
        if reason:
            kept_releases.add(p)
            items.append(Item(p, tree_size(p), reason, action="keep", protected=True))
        else:
            items.append(Item(p, tree_size(p), "old release build"))

    # AppImages, newest first; keep the newest N and any sidecar-matching kept name.
    appimages: list[Path] = []
    if dist_dir.is_dir():
        appimages = sorted((p for p in dist_dir.glob("*.AppImage") if p.is_file()),
                           key=lambda p: p.lstat().st_mtime, reverse=True)
    for i, p in enumerate(appimages):
        if i < args.keep_appimages:
            continue
        reason = protected_reason(p, roots, open_set, args.min_age_hours)
        if reason:
            items.append(Item(p, p.lstat().st_size, reason, action="keep", protected=True))
        else:
            sidecar = p.with_name(p.name + ".sha256")
            items.append(Item(p, p.lstat().st_size, "old AppImage",
                              extra=[sidecar] if sidecar.exists() else []))

    # Temporary directories.
    if temp_dir.is_dir():
        for p in temp_dir.iterdir():
            if age_hours(p) < args.min_age_hours:
                continue
            reason = protected_reason(p, roots, open_set, args.min_age_hours)
            if reason:
                items.append(Item(p, tree_size(p), reason, action="keep", protected=True))
            else:
                items.append(Item(p, tree_size(p), "stale temp directory"))

    # Optional evidence budget (disabled unless --include-evidence is passed).
    if args.include_evidence:
        evidence_dir = (SHARED_ROOT / args.evidence_dir).resolve()
        if evidence_dir.is_dir():
            ev = sorted((p for p in evidence_dir.iterdir()),
                        key=lambda p: p.lstat().st_mtime, reverse=True)
            total = sum(tree_size(p) for p in ev)
            for p in reversed(ev):
                if total <= args.evidence_max_gb * 1024**3:
                    break
                if age_hours(p) < args.evidence_max_age_days * 24:
                    continue
                size = tree_size(p)
                items.append(Item(p, size, "evidence over budget"))
                total -= size

    # Budget: drop the oldest non-protected release builds until under budget.
    total = sum(i.size for i in items if i.action == "delete") + sum(tree_size(p) for p in kept_releases)
    for item in sorted((i for i in items if i.action == "delete" and i.path.parent == release_root),
                       key=lambda i: i.path.lstat().st_mtime):
        if total <= args.budget_gb * 1024**3:
            break
        item.note = "over space budget"
        total -= item.size
    return items


def remove(path: Path, root: Path) -> None:
    resolved = path.resolve()
    root = root.resolve()
    if resolved == root or root not in resolved.parents:
        raise RuntimeError(f"refusing to delete outside {root}: {resolved}")
    if path.is_symlink() or path.is_file():
        path.unlink(missing_ok=True)
        return
    def onerror(func, p, exc):
        try:
            os.chmod(p, 0o700)
            func(p)
        except OSError:
            raise
    shutil.rmtree(path, onerror=onerror)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", metavar="PATH", default="scripts/artifacts.json",
                    help="JSON policy (default: scripts/artifacts.json if present)")
    ap.add_argument("--apply", action="store_true", help="actually delete (default: dry-run)")
    ap.add_argument("--json", metavar="PATH", help="write the plan/report as JSON")
    ap.add_argument("--keep-releases", type=int, default=DEFAULTS["keep_releases"])
    ap.add_argument("--keep-appimages", type=int, default=DEFAULTS["keep_appimages"])
    ap.add_argument("--budget-gb", type=float, default=DEFAULTS["budget_gb"])
    ap.add_argument("--min-age-hours", type=float, default=DEFAULTS["min_age_hours"])
    ap.add_argument("--ccache-max-gb", type=float, default=DEFAULTS["ccache_max_gb"])
    ap.add_argument("--release-root", default=DEFAULTS["release_root"])
    ap.add_argument("--dist-dir", default=DEFAULTS["dist_dir"])
    ap.add_argument("--temp-dir", default=DEFAULTS["temp_dir"])
    ap.add_argument("--include-evidence", action="store_true")
    ap.add_argument("--evidence-dir", default=DEFAULTS["evidence_dir"])
    ap.add_argument("--evidence-max-gb", type=float, default=DEFAULTS["evidence_max_gb"])
    ap.add_argument("--evidence-max-age-days", type=float, default=DEFAULTS["evidence_max_age_days"])
    ap.add_argument("--trim-ccache", action="store_true", help="enforce --ccache-max-gb and run ccache -c")
    # Policy file: CLI flags still win because they are parsed after set_defaults.
    cfg_path = Path(ap.parse_known_args()[0].config)
    if not cfg_path.is_absolute():
        cfg_path = SHARED_ROOT / cfg_path
    if cfg_path.is_file():
        try:
            policy = json.loads(cfg_path.read_text())
            allowed = {k: v for k, v in policy.items() if k in DEFAULTS}
            ap.set_defaults(**allowed)
        except (OSError, ValueError) as exc:
            print(f"warning: ignoring {cfg_path}: {exc}", file=sys.stderr)
    args = ap.parse_args()

    items = collect(args)
    deletions = [i for i in items if i.action == "delete"]
    kept = [i for i in items if i.action == "keep"]
    total = sum(i.size for i in deletions)

    mode = "APPLY" if args.apply else "DRY-RUN"
    print(f"artifact-gc [{mode}] root={SHARED_ROOT}")
    print(f"  release keep={args.keep_releases}  appimage keep={args.keep_appimages}  budget={args.budget_gb:g} GiB")
    if deletions:
        print(f"  reclaimable: {human(total)} in {len(deletions)} item(s)")
        for i in sorted(deletions, key=lambda x: x.size, reverse=True):
            suffix = f" [{i.note}]" if i.note else ""
            print(f"  DELETE {human(i.size):>10}  {i.path.relative_to(SHARED_ROOT)}  ({i.reason}){suffix}")
            for extra in i.extra:
                print(f"         {'':>10}  {extra.relative_to(SHARED_ROOT)}  (sidecar)")
    else:
        print("  nothing to delete")
    for i in kept:
        print(f"  KEEP   {human(i.size):>10}  {i.path.relative_to(SHARED_ROOT)}  ({i.reason})")

    if args.apply:
        for i in deletions:
            for extra in i.extra:
                remove(extra, extra.parent)
            remove(i.path, i.path.parent)
        print(f"  deleted: {human(total)}")
        if args.trim_ccache:
            ccache = SHARED_ROOT / "out/toolchain/ccache/ccache"
            if ccache.is_file():
                env = dict(os.environ, CCACHE_DIR=str(SHARED_ROOT / "out/cache/ccache"))
                subprocess.run([str(ccache), "-M", f"{args.ccache_max_gb:g}G"], env=env, check=False)
                subprocess.run([str(ccache), "-c"], env=env, check=False)
                print(f"  ccache limited to {args.ccache_max_gb:g} GiB and cleaned")

    if args.json:
        report = {
            "mode": mode,
            "root": str(SHARED_ROOT),
            "reclaimableBytes": total,
            "deletions": [{"path": str(i.path), "bytes": i.size, "reason": i.reason,
                           "sidecars": [str(e) for e in i.extra]} for i in deletions],
            "kept": [{"path": str(i.path), "bytes": i.size, "reason": i.reason} for i in kept],
        }
        Path(args.json).write_text(json.dumps(report, indent=2) + "\n")
        print(f"  report: {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
