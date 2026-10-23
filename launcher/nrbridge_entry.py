#!/usr/bin/env python3
"""Normal-launch entry for the private bbport NRBridge AppImage preview.

The package is read-only.  This program creates a writable ``NRBRIDGE_HOME``
containing exactly the files the canonical daily runner expects:

  build/baselines/<sha>/...        checked Native four-artifact build
  build/helper-runtime-model/...   checked Helper PE + derived manifest
  localinputs/runtime.json         user-selected paths + generated pins
  runs/                            the daily runner's private sessions

The game, Wine/GE, NGX directory and the proprietary NR model are external
selections.  The model is validated and referenced, never copied.  Daily play
then execs the coordinator's ``tools/nr_dev/play.py --daily`` so this entry does
not invent a second lifecycle, budgets or retirement rules.
"""
from __future__ import annotations

import argparse
import hashlib
import fcntl
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

APP = Path(os.environ.get("NRBRIDGE_APP", Path(__file__).resolve().parent.parent)).expanduser()  # <AppDir>/usr/share/bbport
BUNDLE = APP / "nrbridge"
TOOLS = APP / "tools/nr_dev"
sys.path.insert(0,str(TOOLS))
import fg_config
import dimension_profile
BUNDLE_MANIFEST = APP / "nrbridge-bundle.json"
# Keep in sync with packaging/nrbridge/closure.py.  These libraries belong to
# the checked Native build and are staged under build/baselines/<sha>/gpu and
# build/baselines/<sha>/; a copy in the generic home/lib closure would shadow
# the pinned source through the runner's LD_LIBRARY_PATH.
PROJECT_LIBRARY_PREFIXES = (
    "libbbgpu.so",
    "libbbnet.so",
    "libbbnet20.so",
    "libbbport_dlss.so",
    "libnvidia-ngx-dlss.so",
)
DEFAULT_HOME = Path(
    os.environ.get("NRBRIDGE_HOME", "")
    or (Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "bbport/nrbridge-home")
).expanduser()


def log(message: str) -> None:
    print("nrbridge: " + message, flush=True)


def die(message: str, code: int = 2) -> "NoReturn":
    print("nrbridge: " + message, file=sys.stderr, flush=True)
    raise SystemExit(code)


_BUNDLE_CACHE: dict | None = None


def bundle_info() -> dict:
    """Identity of the artifacts the builder actually selected.

    This file is written by packaging/nrbridge/build.py from each input's own
    manifest; the launcher must not assume a Native commit, Helper build id or
    protocol revision.
    """
    global _BUNDLE_CACHE
    if _BUNDLE_CACHE is None:
        if not BUNDLE_MANIFEST.is_file():
            die(f"packaged bundle manifest missing: {BUNDLE_MANIFEST}")
        record = json.loads(BUNDLE_MANIFEST.read_text())
        if record.get("schema") != "bbport-nrbridge-bundle-v1":
            die(f"unexpected bundle manifest schema: {record.get('schema')!r}")
        for key in ("native", "helper", "forwarder"):
            if not isinstance(record.get(key), dict):
                die(f"bundle manifest is missing {key}")
        _BUNDLE_CACHE = record
    return _BUNDLE_CACHE


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_runtime_inputs():
    path = TOOLS / "runtime_inputs.py"
    if not path.is_file():
        die(f"packaged runtime_inputs.py missing: {path}")
    spec = importlib.util.spec_from_file_location("nrbridge_runtime_inputs", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def canonical(value: str, *, directory: bool = False) -> Path:
    """Same path rules the dev runtime configuration uses (no symlink aliases)."""
    if not isinstance(value, str) or not value or "\x00" in value or "\n" in value or ":" in value:
        die(f"unsafe path (absolute, no ':' or newline): {value!r}")
    path = Path(value)
    if not path.is_absolute() or path.resolve() != path or path.is_symlink():
        die(f"canonical absolute path required: {value}")
    if directory and not path.is_dir():
        die(f"directory missing: {value}")
    if not directory and not path.is_file():
        die(f"file missing: {value}")
    return path


def canonical_option(value: str, *, directory: bool = False) -> Path:
    """A launcher-supplied path that may not exist yet.

    The root tools perform the authoritative whitelist/snapshot checks; this
    only refuses relative paths, symlinks and newline/colon injection so the
    value forwarded to play.py is unambiguous.
    """
    if not isinstance(value, str) or not value or "\x00" in value or "\n" in value or ":" in value:
        die(f"unsafe path (absolute, no ':' or newline): {value!r}")
    path = Path(value).expanduser()
    if not path.is_absolute() or path.resolve() != path or path.is_symlink():
        die(f"canonical absolute path required: {value}")
    if directory and path.exists() and not path.is_dir():
        die(f"not a directory: {value}")
    return path


def launcher_settings_path() -> Path:
    return Path(
        os.environ.get(
            "NRBRIDGE_LAUNCHER_SETTINGS",
            os.environ.get("BB_LAUNCHER_SETTINGS", os.environ.get("NRBRIDGE_ONLINE_CONFIG",
            str(Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
                / "bbport-launcher/settings.json"))),
        )
    ).expanduser()


def desktop_launcher_path() -> Path:
    return Path(__file__).resolve().with_name("bbport_launcher.py")


def desktop_launcher(args) -> int:
    """Hand a desktop launch to the full bbport launcher.

    The entry stays in the same process (exec), so the AppImage/FUSE mount
    lives exactly as long as the launcher window.  The package manifest is
    what activates NRBridge in the full launcher; ordinary source checkouts
    keep their old launch path.
    """
    launcher = desktop_launcher_path()
    if not launcher.is_file():
        die(f"full bbport launcher missing: {launcher}")
    home = canonical_home(args.home or DEFAULT_HOME)
    env = dict(os.environ)
    settings = (
        canonical_option(args.launcher_settings)
        if getattr(args, "launcher_settings", None)
        else launcher_settings_path()
    )
    ini_value = (
        getattr(args, "launcher_ini", None)
        or os.environ.get("BB_CONFIG")
        or str((Path(os.environ.get("BB_DATA_DIR", "")) if os.environ.get("BB_DATA_DIR") else home / "player-data") / "bbport.ini")
    )
    data_value = (
        getattr(args, "player_data_dir", None)
        or os.environ.get("BB_DATA_DIR")
        or str(home / "player-data")
    )
    ini = canonical_option(ini_value)
    data = canonical_option(data_value, directory=True)
    env.update(
        {
            "BB_NRBRIDGE_ACTIVE": "1",
            "NRBRIDGE_APP": str(APP),
            "NRBRIDGE_ENTRY": str(Path(__file__).resolve()),
            "NRBRIDGE_HOME": str(home),
            "NRBRIDGE_LAUNCHER_SETTINGS": str(settings),
            "BB_LAUNCHER_SETTINGS": str(settings),
            "NRBRIDGE_LAUNCHER_INI": str(ini),
            "BB_CONFIG": str(ini),
            "NRBRIDGE_PLAYER_DATA_DIR": str(data),
            "BB_DATA_DIR": str(data),
        }
    )
    if settings.name == "settings.json" and settings.parent.name == "bbport-launcher":
        env["XDG_CONFIG_HOME"] = str(settings.parent.parent)
    os.chdir(str(APP))
    command = [sys.executable, str(launcher)]
    if getattr(args, "play", False):
        command.append("--play")
    os.execve(sys.executable, command, env)
    return 0


def pe_dll(path: Path) -> bool:
    try:
        with path.open("rb") as stream:
            if stream.read(2) != b"MZ":
                return False
            stream.seek(0x3C)
            offset = int.from_bytes(stream.read(4), "little")
            stream.seek(offset)
            return stream.read(4) == b"PE\0\0"
    except OSError:
        return False


def default_game_dir() -> str:
    settings = Path(
        os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")
    ) / "bbport-launcher/settings.json"
    try:
        value = json.loads(settings.read_text()).get("game_dir", "")
    except (OSError, ValueError):
        value = ""
    return value or os.environ.get("BB_GAME_DIR", "")


def default_ge() -> str:
    roots = [
        Path.home() / ".local/share/Steam/compatibilitytools.d",
        Path.home() / ".steam/steam/compatibilitytools.d",
        Path.home() / ".var/app/com.valvesoftware.Steam/data/Steam/compatibilitytools.d",
    ]
    found = []
    for root in roots:
        if not root.is_dir():
            continue
        for candidate in sorted(root.glob("GE-Proton*/files")):
            if (candidate / "lib/wine/x86_64-unix/wine").is_file():
                found.append((candidate.stat().st_mtime, str(candidate)))
    return max(found)[1] if found else ""


def default_ngx() -> str:
    for value in (
        "/usr/lib/nvidia/wine",
        "/usr/lib64/nvidia/wine",
        "/usr/lib/wine",
    ):
        path = Path(value)
        if (path / "nvngx.dll").is_file() and (path / "_nvngx.dll").is_file():
            return value
    return ""


def default_model() -> str:
    value = os.environ.get("NRBRIDGE_MODEL", "")
    return value


def default_system_ngx() -> str:
    candidates = []
    for pattern in ("/usr/lib/libnvidia-ngx.so.*", "/usr/lib64/libnvidia-ngx.so.*"):
        candidates.extend(Path("/").glob(pattern.lstrip("/")))
    candidates = [p for p in candidates if p.is_file()]
    return str(sorted(candidates)[-1]) if candidates else ""


def default_sysroot() -> str:
    # The AppImage's own pruned runtime library directory, copied to the
    # writable home so the runner's LD_LIBRARY_PATH and pins have a stable path.
    bundled = APP.parent.parent / "lib/bbport"
    return str(bundled) if bundled.is_dir() else ""


def ask(label: str, default: str, *, required: bool = True) -> str:
    if default:
        prompt = f"{label} [{default}]: "
    else:
        prompt = f"{label}: "
    try:
        answer = input(prompt).strip()
    except EOFError:
        die("input closed; pass the path arguments to --setup")
    value = answer or default
    if required and not value:
        die(f"{label} is required")
    return value


def validate_selection(args) -> dict[str, Path]:
    game = canonical(args.game_dir, directory=True)
    if not (game / "eboot.bin").is_file():
        die(f"game folder has no eboot.bin: {game}")
    ge = canonical(args.ge_files, directory=True)
    for rel in ("lib/wine/x86_64-unix/wine", "lib/wine/x86_64-unix/wine-preloader"):
        if not (ge / rel).is_file():
            die(f"Wine/GE files directory is missing {rel}: {ge}")
    ngx = canonical(args.ngx_dir, directory=True)
    for rel in ("nvngx.dll", "_nvngx.dll"):
        if not (ngx / rel).is_file():
            die(f"NGX Wine directory is missing {rel}: {ngx}")
    model = canonical(args.model)
    if not pe_dll(model):
        die(f"model is not a PE DLL: {model}")
    system_ngx = canonical(args.system_ngx)
    sysroot = canonical(args.sysroot_lib, directory=True)
    info = bundle_info()
    forwarder = canonical(str(BUNDLE / "forwarder/nvngx.dll_dlssnr.dll"))
    if sha256(forwarder) != info["forwarder"]["artifact_sha256"]:
        die("bundled forwarder SHA does not match the bundle manifest")
    helper = canonical(str(BUNDLE / "wine_runtime_model.exe"))
    if sha256(helper) != info["helper"]["artifact_sha256"]:
        die("bundled Helper PE SHA does not match the bundle manifest")
    return {
        "game": game,
        "ge": ge,
        "ngx": ngx,
        "model": model,
        "system_ngx": system_ngx,
        "sysroot": sysroot,
        "forwarder": forwarder,
        "helper": helper,
    }


def _is_project_library(name: str) -> bool:
    return Path(name).name.startswith(PROJECT_LIBRARY_PREFIXES)


def project_libraries(root: Path) -> list[str]:
    if not root.is_dir():
        return []
    return sorted(path.name for path in root.iterdir() if _is_project_library(path.name))


def canonical_home(value: Path) -> Path:
    home = Path(value).expanduser()
    if not home.is_absolute() or home.resolve() != home or home.is_symlink():
        die(f"NRBRIDGE_HOME must be a canonical absolute path: {home}")
    return home


def ensure_runs(home: Path) -> Path:
    """Create the runner's canonical runs directory on a fresh home.

    The runner does ``(home/'runs'/session).mkdir(mode=0o700)`` without
    ``parents=True``, so ``runs`` itself must exist and must not be a symlink
    or a regular file that happens to share the name.
    """
    runs = home / "runs"
    if runs.is_symlink():
        die(f"refusing symlinked runs directory: {runs}", 3)
    if runs.exists() and not runs.is_dir():
        die(f"runs path is not a directory: {runs}", 3)
    runs.mkdir(mode=0o700, parents=True, exist_ok=True)
    if runs.is_symlink() or runs.resolve() != runs or not runs.is_dir():
        die(f"runs directory is not canonical: {runs}", 3)
    return runs


def verify_private_library_dir(home: Path) -> None:
    stale = project_libraries(home / "lib")
    if stale:
        die(
            f"stale project-owned libraries in {home / 'lib'}: {', '.join(stale)}; "
            "this home was created by an older package. Move those duplicate "
            "libraries aside and refresh the home's active runtime pins before "
            "launching. Do not use --setup --force: it re-creates the empty seed.",
            3,
        )


def safe_copy_tree(src: Path, dst: Path, *, replace: bool) -> None:
    if dst.exists():
        if not replace:
            return
        shutil.rmtree(dst)
    shutil.copytree(src, dst, symlinks=False)


def stage_native(home: Path, *, replace: bool) -> Path:
    build = home / "build/baselines" / bundle_info()["native"]["source"]
    source = APP / "bin"
    provenance = APP / "build-provenance"
    if not source.is_dir() or not provenance.is_dir():
        die("AppImage is missing its staged Native build")
    if build.is_dir() and not replace:
        return build
    build.mkdir(parents=True, exist_ok=True)
    for child in source.iterdir():
        target = build / child.name
        if child.is_dir():
            safe_copy_tree(child, target, replace=replace)
        else:
            if target.exists() and replace:
                target.unlink()
            shutil.copy2(child, target)
            target.chmod(0o755)
    for child in provenance.iterdir():
        target = build / child.name
        if target.exists() and replace:
            target.unlink()
        shutil.copy2(child, target)
    return build


def stage_helper(home: Path, *, replace: bool) -> Path:
    info = bundle_info()["helper"]
    dest = home / "build/helper-runtime-model" / info["build_id"]
    exe = dest / "wine_runtime_model.exe"
    if exe.is_file() and not replace:
        return exe
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copy2(BUNDLE / "wine_runtime_model.exe", exe)
    exe.chmod(0o755)
    manifest = json.loads((BUNDLE / "helper-build.json").read_text())
    # The content-addressed manifest remains a receipt for the artifact; only
    # its absolute output path is rebased to this writable home.
    manifest["artifact"] = str(exe)
    manifest["packaged_derivation"] = {
        "schema": "bbport-nrbridge-helper-derivation-v1",
        "artifact_sha256": info["artifact_sha256"],
        "source_commit": manifest.get("source_head"),
        "note": (
            "Helper build manifest and bundled PE SHA are verified together. "
            "The source commit is recorded as the manifest states; this derived "
            "receipt does not claim a same-source rebuild."
        ),
    }
    (dest / "build.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return exe


def stage_forwarder(home: Path) -> Path:
    dest = home / "nrbridge/forwarder"
    dest.mkdir(parents=True, exist_ok=True)
    target = dest / "nvngx.dll_dlssnr.dll"
    shutil.copy2(BUNDLE / "forwarder/nvngx.dll_dlssnr.dll", target)
    target.chmod(0o755)
    return target


def stage_libs(home: Path, source: Path, *, replace: bool) -> Path:
    dest = home / "lib"
    leaked = project_libraries(source)
    if leaked:
        die(f"packaged library closure contains project-owned libraries: {leaked}")
    stale = project_libraries(dest)
    if stale and not replace:
        die(
            f"stale project-owned libraries in {dest}: {', '.join(stale)}; "
            "this home predates the fixed package. Migrate it manually "
            "(duplicates aside, then refresh the active pins). Do not use "
            "--setup --force: it re-creates the empty seed.",
            3,
        )
    if dest.is_dir() and not replace:
        return dest
    safe_copy_tree(source, dest, replace=replace)
    return dest


def stage_seed(home: Path) -> Path:
    seed = home / "seed/empty"
    savedata = seed / "savedata"
    savedata.mkdir(parents=True, exist_ok=True)
    marker = savedata / ".bbport-preview-seed"
    if not marker.exists():
        marker.write_text("bbport NRBridge preview synthetic seed\n")
    manifest = seed / "manifest.json"
    files = []
    for path in sorted(savedata.iterdir()):
        if path.is_file() and not path.is_symlink():
            files.append({"relative": path.name, "sha256": sha256(path)})
    manifest.write_text(
        json.dumps(
            {
                "schema": "nrbridge-preview-seed-v1",
                "note": "synthetic empty seed; no private save data is packaged",
                "files": files,
            },
            indent=2,
        )
        + "\n"
    )
    return seed


def generate_pins(runtime_inputs, config: dict, required: list[Path], trees: list[Path]) -> list[dict]:
    pins: dict[str, dict] = {}

    def add(path: Path) -> None:
        name = str(path)
        if name in pins:
            return
        if not path.is_file():
            die(f"pin target is not a file: {name}")
        pins[name] = {
            "path": name,
            "real_path": str(path.resolve()),
            "bytes": path.stat().st_size,
            "sha256": sha256(path),
        }

    for tree in trees:
        for name in sorted(runtime_inputs.files_under(tree)):
            add(Path(name))
    for path in required:
        add(path)
    return [pins[name] for name in sorted(pins)]


DEPENDENCY_LOCK = Path("/tmp/nrbridge-poc-gpu.lock")


def dependency_guard():
    path = TOOLS / "daily_session_guard.py"
    spec = importlib.util.spec_from_file_location("nrbridge_dependency_guard", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module.SessionGuard()


def write_config(home: Path, sel: dict[str, Path], runtime_inputs, *, force: bool = False) -> Path:
    """Update inputs only while no managed session owns the GPU/runtime trees."""
    with DEPENDENCY_LOCK.open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError("A managed GPU session is running; close it normally before updating NR inputs") from None
        # Admission is read-only. A diagnostic approval never bypasses a boot HOLD.
        dependency_guard().admit()
        return _write_config_unlocked(home, sel, runtime_inputs, force=force)


def _write_config_unlocked(home: Path, sel: dict[str, Path], runtime_inputs, *, force: bool = False) -> Path:
    ensure_runs(home)
    localinputs = home / "localinputs"
    localinputs.mkdir(parents=True, exist_ok=True)
    config_path = localinputs / "runtime.json"
    # Dependency edits preserve the selected seed byte-for-byte. In particular,
    # do not create a synthetic marker or rewrite its manifest on readback/save.
    if config_path.is_file() and not force:
        existing = json.loads(config_path.read_text())
        seed = canonical(existing["seed"], directory=True)
        canonical(str(seed / "manifest.json"))
    else:
        seed = stage_seed(home)
    pins_path = localinputs / "runtime-pins.json"
    helper = stage_helper(home, replace=force)
    build = stage_native(home, replace=force)
    forwarder = stage_forwarder(home)
    libs = stage_libs(home, sel["sysroot"], replace=force)
    config = {
        "schema": "nr-local-runtime-inputs-v1",
        "assets": {"path": str(sel["game"]), "access": "read-only"},
        "seed": str(seed),
        "ge_files": str(sel["ge"]),
        "sysroot_lib": str(libs),
        "helper": {
            "path": str(helper),
            "manifest_sha256": sha256(helper.parent / "build.json"),
        },
        "forwarder": str(forwarder),
        "model": str(sel["model"]),
        "system_ngx": str(sel["system_ngx"]),
        "ngx_dir": str(sel["ngx"]),
        "pins": {"path": str(pins_path), "sha256": ""},
    }
    required = [
        helper,
        helper.parent / "build.json",
        seed / "manifest.json",
        sel["game"] / "eboot.bin",
        forwarder,
        sel["model"],
        sel["system_ngx"],
        sel["ge"] / "lib/wine/x86_64-unix/wine",
        sel["ge"] / "lib/wine/x86_64-unix/wine-preloader",
        sel["ngx"] / "_nvngx.dll",
        sel["ngx"] / "nvngx.dll",
        *(Path(seed / "savedata") / item["relative"] for item in json.loads((seed / "manifest.json").read_text())["files"]),
    ]
    pins = generate_pins(runtime_inputs, config, required, [sel["ge"], libs, sel["ngx"]])
    pins_path.write_text(
        json.dumps({"schema": "nr-runtime-input-pins-v1", "pins": pins}, indent=2) + "\n"
    )
    config["pins"]["sha256"] = sha256(pins_path)
    config_path.write_text(json.dumps(config, indent=2) + "\n")
    (home / "selection.json").write_text(
        json.dumps(
            {
                "schema": "bbport-nrbridge-preview-selection-v1",
                "game": str(sel["game"]),
                "ge_files": str(sel["ge"]),
                "model": str(sel["model"]),
                "model_sha256": sha256(sel["model"]),
                "ngx_dir": str(sel["ngx"]),
                "system_ngx": str(sel["system_ngx"]),
                "native_build": str(build),
                "helper": str(helper),
                "helper_sha256": sha256(helper),
                "forwarder_sha256": sha256(forwarder),
                "config": str(config_path),
                "pins": str(pins_path),
                "pins_sha256": sha256(pins_path),
            },
            indent=2,
        )
        + "\n"
    )
    return config_path


def setup(args) -> Path:
    home = canonical_home(args.home or DEFAULT_HOME)
    args.home = home
    home.mkdir(parents=True, exist_ok=True)
    ensure_runs(home)
    config = home / "localinputs/runtime.json"
    if config.is_file() and not args.force:
        log(f"configuration already exists: {config}")
        check(args)
        return config
    interactive = sys.stdin.isatty() and not all(
        (args.game_dir, args.ge_files, args.model, args.ngx_dir, args.system_ngx)
    )
    if interactive:
        args.game_dir = ask("Game folder (contains eboot.bin)", args.game_dir or default_game_dir())
        args.ge_files = ask("Wine/GE files directory", args.ge_files or default_ge())
        args.model = ask("NR model (nvngx_dlssnr.dll; not copied)", args.model or default_model())
        args.ngx_dir = ask("NGX Wine DLL directory", args.ngx_dir or default_ngx())
        args.system_ngx = ask("System libnvidia-ngx.so", args.system_ngx or default_system_ngx())
    if not args.sysroot_lib:
        args.sysroot_lib = default_sysroot()
    for field in ("game_dir", "ge_files", "model", "ngx_dir", "system_ngx", "sysroot_lib"):
        if not getattr(args, field):
            die(f"--{field.replace('_', '-')} is required (or run --setup in a terminal)")
    sel = validate_selection(args)
    info = bundle_info()
    log(f"Native build: {info['native']['source']}")
    log(
        f"Helper: {info['helper']['build_id']} "
        f"({info['helper']['artifact_sha256'][:16]}…), "
        f"forwarder {info['forwarder']['build_id']}"
    )
    log(
        "protocol: "
        f"wire {info['helper']['protocol'].get('wire_version')} / "
        f"revision {info['helper']['protocol'].get('interface_revision')}"
    )
    log(f"Model: {sel['model']} ({sha256(sel['model'])[:16]}…; external)")
    log(f"GE: {sel['ge']}")
    log(f"NGX: {sel['ngx']} / {sel['system_ngx']}")
    log(f"Preparing {home} (first run copies the checked Native artifacts and library closure)…")
    config = write_config(home, sel, load_runtime_inputs(), force=args.force)
    check(args)
    log(f"ready: {config}")
    return config


def check(args) -> None:
    home = Path(args.home or DEFAULT_HOME).expanduser()
    os.environ["NRBRIDGE_HOME"] = str(home)
    runtime_inputs = load_runtime_inputs()
    config = Path(getattr(args,"local_inputs",None) or home / "localinputs/runtime.json")
    if not config.is_file():
        die(f"no configuration at {config}; run --setup", 3)
    verify_private_library_dir(home)
    try:
        binding = runtime_inputs.load(config, home)
    except Exception as error:  # the validator raises ValueError with a precise reason
        die(f"runtime configuration validation failed: {error}", 3)
    selection = json.loads((home / "selection.json").read_text())
    try:
        import importlib.util
        spec = importlib.util.spec_from_file_location("nrbridge_runner", TOOLS / "runner.py")
        runner = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(runner)
        runner.checked_native_build(home / "build/baselines" / bundle_info()["native"]["source"])
    except Exception as error:
        die(f"Native build validation failed: {error}", 3)
    log(f"native  {selection['native_build']}")
    log(f"helper  {selection['helper']} ({selection['helper_sha256'][:16]}…)")
    log(f"model   {selection['model']} ({selection['model_sha256'][:16]}…) external")
    log(f"config  {config}")
    log(f"pins    {binding['pins_path']} ({binding['pins_sha256'][:16]}…)")
    info = bundle_info()
    if not info.get("daily_ready", False):
        marker = bool(info["native"].get("daily_portable", False))
        revision = info["helper"]["protocol"].get("interface_revision")
        log(
            "WARNING: daily launch is blocked: "
            + ("Native has no NRBRIDGE_NATIVE_DAILY_RING64_V2 marker"
               if not marker else f"Helper interface revision is {revision}, daily requires 4")
            + ". --check is still valid for this preview package."
        )
    log("static configuration OK (no GPU, Wine, account or save operation performed)")


def terminal_command(command: list[str]) -> list[str] | None:
    candidates = [
        ("alacritty", ["alacritty", "-e", *command]),
        ("foot", ["foot", *command]),
        ("kitty", ["kitty", *command]),
        ("wezterm", ["wezterm", "start", "--", *command]),
        ("gnome-terminal", ["gnome-terminal", "--", *command]),
        ("konsole", ["konsole", "-e", *command]),
        ("xfce4-terminal", ["xfce4-terminal", "-e", " ".join(command)]),
        ("xterm", ["xterm", "-e", *command]),
    ]
    for name, argv in candidates:
        if shutil.which(name):
            return argv
    return None


def read_state(home: Path) -> dict:
    path = home / "state.json"
    try:
        value = json.loads(path.read_text())
    except (OSError, ValueError):
        return {}
    return value if isinstance(value, dict) else {}


def write_state(home: Path, state: dict) -> None:
    path = home / "state.json"
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(state, indent=2) + "\n")
    temporary.chmod(0o600)
    temporary.replace(path)


def run_result(home: Path, session: str) -> dict | None:
    path = home / "runs" / session / "result.json"
    try:
        value = json.loads(path.read_text())
    except (OSError, ValueError):
        return None
    return value if isinstance(value, dict) else None


def normal_retired(result: dict | None) -> bool:
    return bool(
        result
        and result.get("status") == "PASS_NORMAL_TEARDOWN"
        and result.get("returncode") == 0
        and not result.get("supervisor_interrupted")
        and result.get("residuals") == []
    )


def choose_continuation(home: Path, args) -> str | None:
    """Reuse the last normally retired session; never silently reuse a HOLD.

    The runner creates a fresh profile from the seed for a new game, so an
    abnormal previous run's written save is left untouched under its own
    ``runs/<session>/profile/user/savedata`` until the player chooses.
    """
    if getattr(args, "continue_session", None):
        return args.continue_session
    if getattr(args, "new_game", False):
        return None
    state = read_state(home)
    last = state.get("last_session")
    good = state.get("last_good_session")
    if not last:
        return None
    result = run_result(home, last)
    config = home / "localinputs/runtime.json"
    current_config = sha256(config) if config.is_file() else None
    same_inputs = state.get("config_sha256") == current_config
    if normal_retired(result) and same_inputs:
        log(f"continuing last normally retired session: {last}")
        return last
    if normal_retired(result):
        log(
            "last session retired normally, but the selected Native/Helper/model/GE "
            "inputs changed; its save is preserved. Choose whether to reuse it."
        )
    status = (result or {}).get("status", "NO_RESULT")
    saved = home / "runs" / last / "profile/user/savedata"
    log(
        f"last session {last} did not retire normally (status={status}); "
        f"its written save is preserved at {saved}"
    )
    if not sys.stdin.isatty() or getattr(args, "no_terminal", False):
        die(
            "abnormal previous session requires a player choice; run --continue-session "
            f"{good or '<session>'} for the last good save, or --new-game. "
            "No save was changed.",
            3,
        )
    allow_good = bool(good and same_inputs)
    choices = "[n] new game"
    if allow_good:
        choices += f", [g] last good session {good}"
    choices += ", [c] name a session, [q] quit"
    while True:
        answer = input(f"Choose: {choices} [{'g' if allow_good else 'n'}]: ").strip().lower()
        if not answer:
            answer = "g" if allow_good else "n"
        if answer in ("n", "new"):
            return None
        if answer in ("g", "good") and allow_good:
            return good
        if answer in ("c", "continue"):
            value = input("session to continue: ").strip()
            if not value:
                continue
            if not normal_retired(run_result(home, value)):
                print(
                    f"{value} is not a normally retired session; its save is kept "
                    "but cannot be auto-continued. Choose another.",
                    flush=True,
                )
                continue
            return value
        if answer in ("q", "quit"):
            raise SystemExit(0)
        print("unknown choice", flush=True)


def record_run(home: Path, session: str) -> dict:
    result = run_result(home, session)
    status = (result or {}).get("status", "NO_RESULT")
    state = read_state(home)
    state["last_session"] = session
    state["last_status"] = status
    state["last_updated"] = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    config = home / "localinputs/runtime.json"
    if config.is_file():
        state["config_sha256"] = sha256(config)
    if normal_retired(result):
        state["last_good_session"] = session
        state["last_good_status"] = status
    write_state(home, state)
    return state


def launcher_online_settings() -> Path:
    return launcher_settings_path()


def online_choice(args) -> tuple[str, Path | None, bool]:
    """Forward the runner's explicit online flags.

    Daily play is offline unless the player selects a pool.  The account file
    is only named here; ``daily_online.py`` reads the current pool's whitelist
    and writes it to the private session profile.  This entry never logs in,
    prints a token, or invokes a token-printing CLI.
    """
    pool = getattr(args, "online_pool", None) or "offline"
    if getattr(args, "offline", False):
        pool = "offline"
    if pool not in ("offline", "shadnet", "thd"):
        die(f"unknown online pool: {pool}")
    if pool == "offline":
        return "offline", None, False
    account_value = getattr(args, "online_account_config", None) or getattr(
        args, "launcher_settings", None
    )
    account = Path(account_value).expanduser() if account_value else launcher_online_settings()
    if not account.is_absolute() or account.resolve() != account or account.is_symlink():
        die(f"online account configuration must be a canonical file: {account}")
    if not account.is_file() or account.stat().st_size > 65536:
        die(f"online account configuration is missing or too large: {account}")
    bbsc = bool(getattr(args, "bbsc", False))
    if bbsc and pool != "shadnet":
        die("BBSC requires the shadnet pool")
    return pool, account, bbsc


def runner_supports_online() -> bool:
    # Sol owns runner/play; do not silently drop a requested online pool if the
    # canonical daily entry has not grown the explicit flags yet.
    try:
        text = (TOOLS / "play.py").read_text()
        extra = (TOOLS / "daily_online.py").read_text()
    except OSError:
        return False
    return (
        "--online-pool" in extra
        and "--online-account-config" in extra
        and "daily_online.add_arguments" in text
    )


def daily(args) -> int:
    home = canonical_home(args.home or DEFAULT_HOME)
    config = Path(getattr(args,"local_inputs",None) or home / "localinputs/runtime.json")
    if not config.is_file():
        if sys.stdin.isatty():
            log("first run: selecting local inputs")
            setup(args)
        else:
            die(f"no configuration at {config}; run this AppImage with --setup in a terminal", 3)
    ensure_runs(home)
    explicit_build=getattr(args,"native_build",None)
    if explicit_build:
        build=canonical_option(explicit_build,directory=True)
        info=None # Existing managed build: play.py verifies its complete provenance.
    else:
        verify_private_library_dir(home)
        info=bundle_info()
        build=home / "build/baselines" / info["native"]["source"]
    if info is not None and not info.get("daily_ready", False):
        marker = bool(info["native"].get("daily_portable", False))
        revision = info["helper"]["protocol"].get("interface_revision")
        die(
            "daily launch is blocked: "
            + ("Native has no NRBRIDGE_NATIVE_DAILY_RING64_V2 marker"
               if not marker else f"Helper interface revision is {revision}, daily requires 4")
            + ". Rebuild with a matching Native/Helper pair; --check remains available.",
            3,
        )
    session = args.session or ("nrbridge-" + time.strftime("%Y%m%d-%H%M%S"))
    launcher_settings = (
        canonical_option(args.launcher_settings)
        if getattr(args, "launcher_settings", None)
        else None
    )
    launcher_ini = (
        canonical_option(args.launcher_ini)
        if getattr(args, "launcher_ini", None)
        else None
    )
    player_data = (
        canonical_option(args.player_data_dir, directory=True)
        if getattr(args, "player_data_dir", None)
        else None
    )
    # A private test session from an earlier preview is never continued just
    # because the packaged launcher supplied its settings.  The runner takes
    # the player's user_dir from that settings file and makes its own private
    # copy; only a real normal retirement writes back.  An explicit
    # --continue-session still wins.
    if launcher_settings and not getattr(args, "continue_session", None) and not getattr(
        args, "new_game", False
    ):
        continuation = None
    else:
        continuation = choose_continuation(home, args)
    pool, online_account, bbsc = online_choice(args)
    command = [
        sys.executable,
        str(TOOLS / "play.py"),
        "--daily",
        "--session",
        session,
        "--local-inputs",
        str(config),
        "--native-build",
        str(build),
    ]
    if launcher_settings:
        command += ["--launcher-settings", str(launcher_settings)]
    if launcher_ini:
        command += ["--launcher-ini", str(launcher_ini)]
    if player_data:
        command += ["--player-data-dir", str(player_data)]
    if getattr(args, "sr_only", False):
        command.append("--sr-only")
    if getattr(args,"fg_backend",None) is not None:
        command += ["--fg-backend",args.fg_backend]
    for key in ("nr_sr_preset","nr_output","nr_execution","reuse_nr_prefix","dlssg_model","dlssg_runtime","dlssg_prefix_session","dlssg_pipeline","dlssg_transport"):
        value=getattr(args,key,None)
        if value is not None:command += ["--"+key.replace("_","-"),str(value)]
    if getattr(args, "experimental_fg", None):
        command += ["--experimental-fg", args.experimental_fg, "--fg-native"]
    if continuation:
        command += ["--continue-session", continuation]
    if getattr(args, "joystick_mouse", False):
        command += ["--joystick-mouse"]
    if pool != "offline":
        if not runner_supports_online():
            die(
                "online was requested but the packaged daily runner has no "
                "--online-pool/--online-account-config flags; refusing to run "
                "offline-only silently. Use --offline or update the runner.",
                3,
            )
        command += ["--online-pool", pool, "--online-account-config", str(online_account)]
        if bbsc:
            command += ["--bbsc"]
        log(
            f"online pool: {pool}; account state: {online_account} "
            "(read by the runner, never printed)"
        )
    env = dict(os.environ)
    env["NRBRIDGE_HOME"] = str(home)
    env["BB_PREBUILT"] = "1"
    env.setdefault("PYTHON", sys.executable)
    if launcher_settings:
        env["NRBRIDGE_LAUNCHER_SETTINGS"] = str(launcher_settings)
        env["BB_LAUNCHER_SETTINGS"] = str(launcher_settings)
    if launcher_ini:
        env["NRBRIDGE_LAUNCHER_INI"] = str(launcher_ini)
        env["BB_CONFIG"] = str(launcher_ini)
    if player_data:
        env["NRBRIDGE_PLAYER_DATA_DIR"] = str(player_data)
        env["BB_DATA_DIR"] = str(player_data)
    if args.print_command:
        # Paths only; the online file is named, its contents are not read here.
        print(" ".join(command))
        return 0
    if not sys.stdin.isatty() and not args.no_terminal:
        argv = terminal_command(command)
        if argv is None:
            die("no terminal emulator found; run from a terminal or use --no-terminal", 3)
        status = subprocess.call(argv, env=env, cwd=str(APP))
    else:
        status = subprocess.call(command, env=env, cwd=str(APP))
    state = record_run(home, session)
    log(
        f"session {session}: {state.get('last_status')}; "
        f"save/profile kept under {home / 'runs' / session}"
    )
    if state.get("last_status") != "PASS_NORMAL_TEARDOWN":
        log("the next launch will ask before reusing or abandoning this save")
    return status


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--home", type=Path)
    parser.add_argument("--setup", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--daily", action="store_true")
    parser.add_argument("--play", action="store_true",
                        help="play with the full launcher saved settings through the managed coordinator")
    parser.add_argument("--build-fsr411", nargs="*",
                        help="unavailable in the managed NRBridge Preview")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--game-dir")
    parser.add_argument("--ge-files")
    parser.add_argument("--model")
    parser.add_argument("--ngx-dir")
    parser.add_argument("--system-ngx")
    parser.add_argument("--sysroot-lib")
    parser.add_argument("--session")
    parser.add_argument("--continue-session")
    parser.add_argument("--new-game", action="store_true")
    parser.add_argument("--joystick-mouse", action="store_true",
                        help="use the compatibility joystick-mouse fallback instead of native mouse")
    parser.add_argument("--online-pool", choices=("offline", "shadnet", "thd"), default="offline",
                        help="daily online pool; default offline")
    parser.add_argument("--online-account-config",
                        help="canonical launcher settings JSON; the runner copies only the pool whitelist")
    parser.add_argument("--bbsc", action="store_true", help="enable shadnet BBSC hooks")
    parser.add_argument("--offline", action="store_true", help="force the default offline pool")
    parser.add_argument("--launcher-settings",
                        help="canonical launcher settings.json forwarded to play.py")
    parser.add_argument("--launcher-ini",
                        help="canonical bbport.ini forwarded to play.py")
    parser.add_argument("--player-data-dir",
                        help="canonical player data directory forwarded to play.py")
    parser.add_argument("--reuse-nr-prefix",default=os.environ.get("NRBRIDGE_REUSE_NR_PREFIX"))
    parser.add_argument("--native-build",default=os.environ.get("NRBRIDGE_NATIVE_BUILD"),help="explicit existing managed development build; no package copy")
    parser.add_argument("--local-inputs",default=os.environ.get("NRBRIDGE_RUNTIME_INPUTS"))
    fg_config.add_argument(parser)
    dimension_profile.add_arguments(parser)
    for name in ("dlssg-model","dlssg-runtime","dlssg-prefix-session","dlssg-pipeline","dlssg-transport"):
        parser.add_argument("--"+name)
    parser.add_argument("--experimental-fg", choices=("fsr31-hudless",),
                        help="opt-in Experimental HUDless FSR FG x2; requires a rebuilt M1 runtime")
    parser.add_argument("--fg-native", action="store_true",
                        help="explicit Native HDR integration (implied by --experimental-fg)")
    parser.add_argument("--sr-only", action="store_true",
                        help="do not start the Helper; the runner owns the SR-only path")
    parser.add_argument("--no-terminal", action="store_true")
    parser.add_argument("--print-command", action="store_true")
    args = parser.parse_args(argv)
    if args.home is None:
        args.home = DEFAULT_HOME
    args.home = Path(args.home).expanduser()
    if args.build_fsr411 is not None:
        die("FSR 4.1.1 DLL conversion is unavailable in NRBridge Preview (separate GPU workflow). "
            "NRBridge 预览版暂不支持转换 FSR 4.1.1 DLL；关闭 NR 后仍可使用准备好的资源。", 2)
    if args.check:
        check(args)
        return 0
    if args.setup:
        setup(args)
        return 0
    if args.daily or args.print_command:
        return daily(args)
    if args.play:
        return desktop_launcher(args)
    # A desktop launch from the packaged NRBridge opens the full bbport
    # launcher (the package manifest activates the NRBridge group there).
    # The old small setup window is only a source-checkout fallback.
    if os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"):
        os.environ["NRBRIDGE_HOME"] = str(args.home)
        if BUNDLE_MANIFEST.is_file():
            return desktop_launcher(args)
        try:
            import nrbridge_gui
        except Exception as error:
            log(f"GTK launcher unavailable ({type(error).__name__}: {error}); using terminal entry")
            return daily(args)
        return nrbridge_gui.main()
    return daily(args)


if __name__ == "__main__":
    raise SystemExit(main())
