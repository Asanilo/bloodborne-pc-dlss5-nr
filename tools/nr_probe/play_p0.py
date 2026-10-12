#!/usr/bin/env python3
"""Thin user entry for Integration-approved P0 cases; defaults to command preview."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
USER_CASE = "p2-quality-user1800"
USER_GATE_STATUS = "PASS_BOUNDED_EXPERIMENTAL_ENTRY_GATE"
STATES = {"Disabled", "Starting", "Ready", "Active", "Idle", "Draining", "Quarantined", "Failed"}


def strict_json(text):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate JSON key: " + key)
            result[key] = value
        return result
    return json.loads(text, object_pairs_hook=unique,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError("nonfinite JSON")))


def bounded_json(path, limit=2097152):
    if path.is_symlink() or not path.is_file() or path.stat().st_size > limit:
        raise ValueError("regular bounded JSON required: " + str(path))
    return strict_json(path.read_text())


def private_path(path, parent):
    path = Path(path).absolute()
    if path.is_symlink() or not path.resolve().is_relative_to(parent.resolve()):
        raise ValueError("private path required under " + str(parent))
    return path.resolve()


def experience_gate(root, approval):
    pin = approval.get("experience_entry_gate")
    if (not isinstance(pin, dict) or set(pin) != {"path", "sha256", "bytes"} or
            not isinstance(pin.get("path"), str) or not Path(pin["path"]).is_absolute() or
            not isinstance(pin.get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", pin["sha256"]) or
            type(pin.get("bytes")) is not int or not 1<=pin["bytes"]<=2097152):
        raise ValueError("Root experience_entry_gate exact path/sha256/bytes pin required")
    path = private_path(pin["path"], root / "integration/playtest")
    if str(path) != pin["path"] or not path.is_file() or path.stat().st_size > 2097152:
        raise ValueError("canonical regular private experience gate required")
    payload = path.read_bytes()
    if len(payload) != pin["bytes"] or hashlib.sha256(payload).hexdigest() != pin["sha256"]:
        raise ValueError("experience gate actual bytes/SHA mismatch")
    # Interpret precisely the bytes whose identity was checked, not a second read.
    gate = strict_json(payload.decode("utf-8"))
    if not isinstance(gate, dict) or gate.get("status") != USER_GATE_STATUS:
        raise ValueError("Root experience entry gate has not passed")
    return pin


def launch_plan(root, session, approval_path):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9-]{0,47}", session):
        raise ValueError("safe base session up to48 characters required")
    approval_path = private_path(approval_path, root / "integration/playtest")
    approval = bounded_json(approval_path)
    if approval.get("status") != "APPROVED_P0_CASE" or not isinstance(approval.get("batch"), dict):
        raise ValueError("Root approval for a new independent bounded batch required; no legacy budget reset")
    # This is a preview, not approval validation. The original runner owns every
    # hash/gate/batch/budget/lock check; no approval or ledger is ever authored here.
    case = approval["case"]
    user_case = case.get("case_id") == USER_CASE
    if (type(case.get("frames")) is not int or case["frames"] not in ((1800,) if user_case else (4,120)) or
            case.get("mode") not in ("resident","skipEvaluate","srOnly") or
            case.get("execution_mode") not in ("hot","diagnostic") or
            type(case.get("initial_enable")) is not bool or
            type(case.get("outer_seconds")) is not int or not 1<=case["outer_seconds"]<=600 or
            type(case.get("helper_start_allowance")) is not int or not 0<=case["helper_start_allowance"]<=2):
        raise ValueError("only bounded4/120 or the exact Root-gated user1800 case; no general longrun")
    gate_pin = None
    if user_case:
        if (case["mode"] != "resident" or case["execution_mode"] != "hot" or
                case["outer_seconds"] != 300 or not 1<=case["helper_start_allowance"]<=2):
            raise ValueError("user1800 must be resident/hot, outer300, Helper allowance1..2")
        gate_pin = experience_gate(root, approval)
    command = [sys.executable, str(root / "integration/run_playtest.py"), session,
               "--approval", str(approval_path)]
    return {"status": "COMMAND_PREVIEW_NOT_PREFLIGHT_OR_RUNTIME_PASS", "GPU": "NOT_RUN",
            "case_id": case["case_id"], "mode": case["mode"],
            "frames": case["frames"], "execution_mode": case["execution_mode"],
            "initial_enable": case["initial_enable"], "outer_seconds": case["outer_seconds"],
            "helper_start_allowance": case["helper_start_allowance"],
            "experience_entry_gate": gate_pin,
            "effective_NR_interaction_window_max_seconds": 60 if user_case else None,
            "interaction_window_enforcement": "Root gated runtime, not this preview;1800 is a hard cap, not a test PASS" if user_case else None,
            "prepare_profile_command": [sys.executable, str(root / "integration/prepare_playtest_profile.py"), session],
            "check_command": command + ["--preflight-only"], "launch_command": command,
            "needs": "Root reviews/freezes a new case and creates a fresh verified private profile first"}


def input_command(root, invocation, enable=None, dump=False, buttons=(), hold=.15):
    inv = private_path(invocation, root / "runs")
    if not math.isfinite(hold) or not 0<=hold<=30:
        raise ValueError("finite hold0..30s required")
    command = [sys.executable, str(root / "integration/input_playtest.py"), "--invocation", str(inv)]
    if enable is not None:
        command += ["--enable", enable]
    if dump:
        command += ["--dump"]
    if buttons:
        command += ["--hold", str(hold), *buttons]
    if not (enable or dump or buttons):
        raise ValueError("choose Enable, dump or a recognized private pad button")
    return command


def read_status(root, invocation):
    inv = private_path(invocation, root / "runs")
    manifest = bounded_json(inv / "manifest.json")
    session = manifest["session"]
    if manifest.get("protocol_version") != 6 or not isinstance(session, str):
        raise ValueError("P0 invocation identity required")
    run = private_path(manifest["run"], root / "bbport/runs")
    if run.name != "p0-game-" + session or (run / "profile/.p0-profile").read_text().strip() != session:
        raise ValueError("private case/marker mismatch")
    path = run / "frontend-status.json"
    if not path.exists():
        return {"state": "NOT_OBSERVED", "scope": "read-only snapshot; not liveness/retirement proof"}
    status = bounded_json(path, 65536)
    if status.get("schema") != "p0-frontend-status-v1" or status.get("session") != session or status.get("state") not in STATES:
        raise ValueError("native status identity/state mismatch")
    for key in ("requested_enable", "applied_enable"):
        if type(status.get(key)) is not bool:
            raise ValueError("native status boolean required: " + key)
    for key in ("external_revision", "helper_starts", "finalized_count", "accepted_for_merge_count", "fallback_count"):
        if type(status.get(key)) is not int or status[key] < 0:
            raise ValueError("native unsigned status count required: " + key)
    keys = ("requested_enable", "applied_enable", "state", "external_revision", "helper_starts", "finalized_count",
            "accepted_for_merge_count", "fallback_count", "fallback_reason", "nr_gpu_ms",
            "bypass_disabled_count", "bypass_starting_count", "bypass_unsupported_count")
    return {"status": {key: status.get(key) for key in keys},
            "scope": "read-only possibly stale snapshot; accepted_for_merge is not unique Present; no retirement/liveness claim",
            "SR_timing": "Overlay latest completed source, or P0_SR_GPU_TIMING in game.log; frontend sr_gpu_ms remains legacy null"}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    for action in ("plan", "check", "launch"):
        p = sub.add_parser(action)
        p.add_argument("session")
        p.add_argument("--approval", type=Path, required=True)
        if action != "plan":
            p.add_argument("--execute-approved", action="store_true", help="delegate to the original Root tool; default prints command")
    p = sub.add_parser("input")
    p.add_argument("--invocation", type=Path, required=True)
    p.add_argument("--enable", choices=("on", "off"))
    p.add_argument("--dump", action="store_true")
    p.add_argument("--button", action="append", default=[], choices=("cross", "circle", "square", "triangle", "up", "down", "left", "right", "options", "l1", "r1", "l2", "r2", "l3", "r3"))
    p.add_argument("--hold", type=float, default=.15)
    p.add_argument("--execute-approved", action="store_true")
    p = sub.add_parser("status")
    p.add_argument("--invocation", type=Path, required=True)
    a = parser.parse_args(argv)
    try:
        if a.action == "status":
            print(json.dumps(read_status(ROOT, a.invocation), indent=2)); return 0
        if a.action in ("plan", "check", "launch"):
            plan = launch_plan(ROOT, a.session, a.approval)
            if a.action == "plan":
                print(json.dumps(plan, indent=2)); return 0
            command = plan["check_command" if a.action == "check" else "launch_command"]
        else:
            command = input_command(ROOT, a.invocation, a.enable, a.dump, a.button, a.hold)
        print(json.dumps({"command": command, "shell_preview": shlex.join(command),
                          "execution": "DELEGATE_ROOT" if a.execute_approved else "PREVIEW_ONLY", "GPU": "NOT_RUN_BY_PREVIEW"}), flush=True)
        if a.execute_approved:
            return subprocess.run(command, cwd=ROOT, check=False).returncode
        return 0
    except (ValueError, KeyError, TypeError, OSError) as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    raise SystemExit(main())
