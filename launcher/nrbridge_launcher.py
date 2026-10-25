"""NRBridge glue for the full bbport launcher.

This module deliberately has no GTK dependency: path resolution, the
``localinputs/runtime.json`` readback, the daily command line and the
normal-close policy are plain functions so they can be exercised on CPU.
The full launcher still owns the window; this is not a second launcher.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import shlex
import uuid
import sys
import time
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/nr_dev'))
import fg_config
import dimension_profile

# The checked NRBridge values.  They are applied only while NR is enabled;
# the ordinary launcher settings stay available when NR is switched off.
NR_INI = {
    "upscaler": "dlss",
    "preset": "1",
    "aspect_ratio": "16:9",
    "output_res": "1920x1080",
    "object_motion": "1",
    "jitter": "1",
}
NR_MEMORY = "old"

# Extra variables that would let the UI bypass the runner's renderer,
# synchronisation or memory checks.  The runner still validates everything;
# the launcher refuses to pretend these are ordinary settings.
DANGEROUS_ENV_PREFIXES = (
    "BB_NR_",
    "BB_FRAME_GEN",
    "BB_DLSSG_",
    "NVPRESENT_",
    "BB_HOOK",
    "BB_SYNC",
    "BB_MEMORY",
    "BB_LAYER_MEMORY",
    "BB_RENDER",
    "BB_PC_MODEL",
    "BB_AS_0_3",
    "BB_GPU_ID",
    "BB_NATIVE_MOUSE",
    "LD_PRELOAD",
    "LD_LIBRARY_PATH",
    "VK_",
)


def app_dir() -> Path:
    value = os.environ.get("NRBRIDGE_APP")
    return Path(value).expanduser() if value else Path(__file__).resolve().parent.parent


def bundle_manifest() -> Path:
    return app_dir() / "nrbridge-bundle.json"


def is_packaged() -> bool:
    return bundle_manifest().is_file()


def active() -> bool:
    """True for the packaged NRBridge, or when the entry marks this launch."""
    if os.environ.get("BB_NRBRIDGE_ACTIVE") == "1":
        return True
    return is_packaged()


def home() -> Path:
    value = os.environ.get("NRBRIDGE_HOME")
    if value:
        return Path(value).expanduser()
    return Path(
        os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")
    ) / "bbport/nrbridge-home"


def launcher_settings() -> Path:
    value = os.environ.get("NRBRIDGE_LAUNCHER_SETTINGS") or os.environ.get(
        "BB_LAUNCHER_SETTINGS"
    )
    if value:
        return Path(value).expanduser()
    return Path(
        os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")
    ) / "bbport-launcher/settings.json"


def player_data() -> Path:
    value = os.environ.get("NRBRIDGE_PLAYER_DATA_DIR") or os.environ.get("BB_DATA_DIR")
    return Path(value).expanduser() if value else home() / "player-data"


def launcher_ini() -> Path:
    value = os.environ.get("NRBRIDGE_LAUNCHER_INI") or os.environ.get("BB_CONFIG")
    return Path(value).expanduser() if value else player_data() / "bbport.ini"


def entry_path() -> Path:
    value = os.environ.get("NRBRIDGE_ENTRY")
    return Path(value).expanduser() if value else app_dir() / "launcher/nrbridge_entry.py"


def runtime_config(home_path=None) -> Path:
    override=os.environ.get("NRBRIDGE_RUNTIME_INPUTS")
    return Path(override) if override else Path(home_path or home()) / "localinputs/runtime.json"


def read_runtime(home_path=None) -> dict:
    try:
        value = json.loads(runtime_config(home_path).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    return value if isinstance(value, dict) else {}


def dependency_values(runtime: dict | None, settings: dict | None = None) -> dict:
    """The paths shown in the dependency rows, read back from runtime.json."""
    runtime = runtime or {}
    settings = settings or {}
    assets = runtime.get("assets") if isinstance(runtime.get("assets"), dict) else {}
    return {
        "game_dir": assets.get("path") or settings.get("game_dir", ""),
        "ge_files": runtime.get("ge_files", ""),
        "model": runtime.get("model", ""),
        "ngx_dir": runtime.get("ngx_dir", ""),
        "system_ngx": runtime.get("system_ngx", ""),
        "sysroot_lib": runtime.get("sysroot_lib", ""),
    }


def logs_directory(session: str | None = None, home_path=None) -> Path:
    """Open an existing current/last run, without creating an unused session."""
    root = Path(home_path or home())
    try:
        state = json.loads((root / "state.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        state = {}
    last = state.get("last_session") if isinstance(state, dict) else None
    for name in (session, last):
        if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9-]{0,62}", name):
            continue
        candidate = root / "runs" / name
        if candidate.is_dir() and candidate.resolve() == candidate:
            return candidate
    return root / "runs"


def read_result(home_path, session: str) -> dict | None:
    try:
        value = json.loads(
            (Path(home_path) / "runs" / session / "result.json").read_text(
                encoding="utf-8"
            )
        )
    except (OSError, ValueError):
        return None
    return value if isinstance(value, dict) else None


def result_status(result: dict | None) -> str:
    return str((result or {}).get("status") or "NO_RESULT")


def result_is_hold(result: dict | None) -> bool:
    return "HOLD" in result_status(result).upper()


def session_name() -> str:
    return "nrbridge-" + time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]


def online_mapping(settings: dict, account: Path | str | None = None):
    """Map the original launcher Online page to the runner's explicit pool."""
    if not settings.get("online"):
        return "offline", None, False
    original = str(settings.get("online_pool") or "seamless")
    if original in ("huntersdream", "thd"):
        pool = "thd"
    elif original in ("seamless", "shadnet"):
        pool = "shadnet"
    else:
        pool = "offline"
    if pool == "offline":
        return pool, None, False
    path = Path(account).expanduser() if account else launcher_settings()
    bbsc = bool(settings.get("online_bbsc")) and pool == "shadnet"
    return pool, path, bbsc


def launch_environment(
    *, home_path=None, settings_path=None, ini_path=None, data_dir=None
) -> dict[str, str]:
    """Environment for the entry; never forwards the game's renderer extras."""
    env = dict(os.environ)
    settings_path = Path(settings_path or launcher_settings()).expanduser()
    ini_path = Path(ini_path or launcher_ini()).expanduser()
    data_dir = Path(data_dir or player_data()).expanduser()
    home_path = Path(home_path or home()).expanduser()
    env.update(
        {
            "BB_NRBRIDGE_ACTIVE": "1",
            "NRBRIDGE_APP": str(app_dir()),
            "NRBRIDGE_ENTRY": str(entry_path()),
            "NRBRIDGE_HOME": str(home_path),
            "NRBRIDGE_LAUNCHER_SETTINGS": str(settings_path),
            "BB_LAUNCHER_SETTINGS": str(settings_path),
            "NRBRIDGE_LAUNCHER_INI": str(ini_path),
            "BB_CONFIG": str(ini_path),
            "NRBRIDGE_PLAYER_DATA_DIR": str(data_dir),
            "BB_DATA_DIR": str(data_dir),
        }
    )
    if settings_path.name == "settings.json" and settings_path.parent.name == "bbport-launcher":
        env["XDG_CONFIG_HOME"] = str(settings_path.parent.parent)
    return env


def daily_command(
    *,
    session: str,
    home_path=None,
    settings_path=None,
    ini_path=None,
    data_dir=None,
    nr_enabled: bool = True,
    native_mouse: bool = True,
    experimental_fg: bool = False,
    fg_selection: dict | None = None,
    nr_dimensions: dict | None = None,
    online_pool: str = "offline",
    account_config=None,
    bbsc: bool = False,
    entry=None,
) -> list[str]:
    """The one daily path: entry --daily --no-terminal plus config paths.

    The runner (not this adapter and not run.sh) owns launch, snapshot,
    whitelist, save copy and retirement.
    """
    settings_path = Path(settings_path or launcher_settings()).expanduser()
    command = [
        sys.executable,
        str(Path(entry or entry_path()).expanduser()),
        "--daily",
        "--no-terminal",
        "--home",
        str(Path(home_path or home()).expanduser()),
        "--session",
        session,
        "--launcher-settings",
        str(settings_path),
        "--launcher-ini",
        str(Path(ini_path or launcher_ini()).expanduser()),
        "--player-data-dir",
        str(Path(data_dir or player_data()).expanduser()),
    ]
    if nr_dimensions:
        command += ["--nr-sr-preset",nr_dimensions["preset_name"],"--nr-output",
                    f'{nr_dimensions["output_width"]}x{nr_dimensions["output_height"]}',
                    "--nr-execution",f'{nr_dimensions["model_width"]}x{nr_dimensions["model_height"]}']
    if not nr_enabled:
        command.append("--sr-only")
    if fg_selection is not None:
        command += fg_config.command_options(fg_selection)
    elif experimental_fg:
        command += ["--experimental-fg", "fsr31-hudless", "--fg-native"]
    if not native_mouse:
        command.append("--joystick-mouse")
    if online_pool and online_pool != "offline":
        command += ["--online-pool", online_pool]
        command += [
            "--online-account-config",
            str(Path(account_config or settings_path).expanduser()),
        ]
        if bbsc and online_pool == "shadnet":
            command.append("--bbsc")
    return command


def stop_command(session: str, *, runner=None) -> list[str]:
    runner = Path(runner) if runner else app_dir() / "tools/nr_dev/runner.py"
    return [sys.executable, str(runner), "stop", session]


def request_normal_stop(session: str, *, spawn=subprocess.Popen, env=None, runner=None):
    """Ask the coordinator for a normal window close; never signal the game."""
    return spawn(
        stop_command(session, runner=runner),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=env,
    )


def force_kill_allowed(nrbridge_active: bool) -> bool:
    return not nrbridge_active


def can_close(process_alive: bool, result: dict | None = None) -> bool:
    """The GUI may close only after the actual child process has exited."""
    return not process_alive and not result_is_hold(result)


def nr_conflicts(settings: dict, ini: dict, *, dimensions=False) -> list[str]:
    """Human-readable conflicts with the checked NR constraints."""
    conflicts = []
    checks = (
        ("upscaler", ini.get("upscaler"), NR_INI["upscaler"]),
        ("preset", str(ini.get("preset", "1")), NR_INI["preset"]),
        ("aspect_ratio", ini.get("aspect_ratio", "16:9"), NR_INI["aspect_ratio"]),
        ("output_res", ini.get("output_res", "1920x1080"), NR_INI["output_res"]),
        ("object_motion", str(ini.get("object_motion", "1")), NR_INI["object_motion"]),
        ("jitter", str(ini.get("jitter", "1")), NR_INI["jitter"]),
    )
    for key, actual, required in checks:
        if dimensions and key in ("preset","aspect_ratio","output_res"):continue
        if dimensions and key=="upscaler" and actual=="off":continue
        if str(actual) != required and not (key == "aspect_ratio" and actual == "auto"):
            conflicts.append(f"{key}={actual} (NRBridge: {required})")
    memory = str(settings.get("memory_model", "auto"))
    if memory not in ("auto", "old", "new"):
        conflicts.append(f"memory_model={memory} (unknown model)")
    if settings.get("as_0_3", False):
        conflicts.append("as_0_3=1 (NRBridge: 0)")
    return conflicts


def dangerous_extra_env(text: str) -> list[str]:
    found = []
    for item in shlex.split(str(text or "")):
        if "=" not in item:
            continue
        key = item.split("=", 1)[0].strip()
        if key.startswith(DANGEROUS_ENV_PREFIXES):
            found.append(key)
    return sorted(set(found))


def save_dependencies(values: dict, home_path=None) -> Path:
    """Validate selections and rewrite runtime.json without replacing the seed.

    ``write_config(force=False)`` keeps the existing Native/Helper/library
    copies, seed contents, runs and state; it only updates the selected paths,
    pins and selection receipt.  It never calls ``--setup --force``.
    """
    import nrbridge_entry as entry

    home_path = Path(home_path or home()).expanduser()
    runtime = read_runtime(home_path)
    game_dir = values.get("game_dir") or dependency_values(runtime).get("game_dir")
    sysroot = (
        values.get("sysroot_lib")
        or runtime.get("sysroot_lib")
        or entry.default_sysroot()
    )
    args = argparse.Namespace(
        home=home_path,
        game_dir=game_dir,
        ge_files=values.get("ge_files"),
        model=values.get("model"),
        ngx_dir=values.get("ngx_dir"),
        system_ngx=values.get("system_ngx"),
        sysroot_lib=sysroot,
    )
    selected = entry.validate_selection(args)
    return entry.write_config(
        home_path, selected, entry.load_runtime_inputs(), force=False
    )


def player_command(settings: dict, ini: dict, *, settings_path=None, ini_path=None,
                   data_dir=None, home_path=None, session=None):
    """Validate the saved selection, then build the shared GUI/--play entry."""
    nr_enabled = bool(settings.get("nrbridge_enabled", True))
    fg=fg_config.selection(fg_config.resolved(settings,home_path or home()))
    if fg["backend"] in ("dlssg","smooth-motion"):fg_config.availability(fg,home_path or home())
    if fg["backend"]=="dlssg":
        conflicts=nr_conflicts(settings,ini,dimensions=fg['transport']=='gpu-dmabuf-v1')
        if ini.get('upscaler')!='dlss' and not(ini.get('upscaler')=='off' and fg['transport']=='gpu-dmabuf-v1'):
            conflicts.append('DLSS FG requires the validated DLSS SR or native scene source handoff')
        if conflicts:raise ValueError("DLSS FG input requirements: " + "; ".join(conflicts))
    dimensions=None
    # Saved SR/native choices use the existing dimensions route automatically.
    # Native FSR and GPU DLSS use the actual scene/UI sizes; CPU DLSS retains
    # its fixed wire layout.
    variable_source = fg['backend'] in ('off','fsr31','fsr316') or (fg['backend']=='dlssg' and fg['transport']=='gpu-dmabuf-v1')
    nonbaseline = any(str(ini.get(k,v))!=v for k,v in NR_INI.items() if k in ('upscaler','preset','output_res','aspect_ratio'))
    # GPU FG consumes the actual SR extent even when no NR Helper is requested.
    # The dimensions protocol describes renderer inputs; it must not depend on F8/NR.
    dynamic_fg = fg['backend'] in ('fsr31','fsr316') or (fg['backend']=='dlssg' and fg['transport']=='gpu-dmabuf-v1')
    if settings.get("nrbridge_dimensions",False) or (variable_source and nonbaseline and (nr_enabled or dynamic_fg)):
        if not variable_source:
            raise ValueError("Variable dimensions require native FSR or DLSS GPU mailbox; CPU retains its baseline")
        try:name=tuple(dimension_profile.PRESETS)[int(ini.get("preset","1"))]
        except (ValueError,IndexError):raise ValueError("Select a supported DLSS SR preset")
        if str(ini.get("preset","1")) not in ("0","1","2","3","4"):raise ValueError("Select a supported DLSS SR preset")
        if ini.get("upscaler")=="off":
            name="native"
        dimensions=dimension_profile.select(argparse.Namespace(nr_sr_preset=name,nr_output=ini.get("output_res","1920x1080"),nr_execution=settings.get("nrbridge_execution","1280x720") or None))
        if ini.get("aspect_ratio","auto") not in ("auto",dimensions["aspect_ratio"]):raise ValueError("Output resolution and aspect ratio must match")
    if nr_enabled:
        conflicts = nr_conflicts(settings, ini,dimensions=bool(dimensions))
        if conflicts:
            raise ValueError("NR requirements: DLSS Quality, 16:9 or Auto, 1920x1080, "
                             "jitter/object motion on, old/new/auto memory; " + "; ".join(conflicts))
    dangerous = dangerous_extra_env(settings.get("extra_env", ""))
    if dangerous:
        raise ValueError("NRBridge reserved environment variables: " + ", ".join(dangerous))
    if settings.get("gpu") and settings.get("nrbridge_gpu_id") is None:
        raise ValueError("Selected GPU was not found; refresh the GPU list or choose Auto")
    pool, account, bbsc = online_mapping(settings, settings_path)
    command = daily_command(
        session=session or session_name(), settings_path=settings_path, ini_path=ini_path,
        data_dir=data_dir, home_path=home_path, nr_enabled=nr_enabled,
        native_mouse=bool(settings.get("nrbridge_native_mouse", True)),
        fg_selection=fg, nr_dimensions=dimensions,
        online_pool=pool, account_config=account, bbsc=bbsc,
    )
    if not settings.get('fg_enabled',True) and fg['backend'] in ('fsr31','fsr316','dlssg'):
        command.append('--fg-start-off')
    return command
