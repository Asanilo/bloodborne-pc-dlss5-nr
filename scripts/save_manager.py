#!/usr/bin/env python3
"""Save snapshots and a Linux session lock shared by GUI and --play launches.

Only user/savedata is restored; caches and configuration are never replaced.
Pre-restore snapshots are retained independently of the ten rotating launch snapshots.
"""
import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import uuid


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=".write-", dir=path.parent)
    try:
        with os.fdopen(fd, "w") as stream:
            json.dump(value, stream)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


@contextmanager
def session_lock(user):
    user.mkdir(parents=True, exist_ok=True)
    with (user / ".bbport-session.lock").open("a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield lock


def inventory(root):
    result = {}
    if root.is_symlink():
        raise ValueError("Save directory must not be a symbolic link")
    for path in sorted(root.rglob("*")):
        if path.is_symlink() or not (path.is_file() or path.is_dir()):
            raise ValueError(f"Unsupported save entry: {path}")
        if path.is_file():
            with path.open("rb") as stream:
                result[str(path.relative_to(root))] = hashlib.file_digest(stream, "sha256").hexdigest()
    return result


def snapshots(user):
    return sorted((p for p in (user / "backups").glob("*")
                   if not p.name.startswith(".") and (p / "manifest.json").is_file()), reverse=True)


def backup(user, reason="launch", keep=10):
    source = user / "savedata"
    if not source.exists():
        return None
    inventory(source)  # refuse symlinks before copytree follows them
    folder = user / "backups"
    folder.mkdir(parents=True, exist_ok=True)
    name = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ") + "-" + reason + "-" + uuid.uuid4().hex[:8]
    stage = Path(tempfile.mkdtemp(prefix=".snapshot-", dir=folder))
    try:
        shutil.copytree(source, stage / "savedata")
        atomic_json(stage / "manifest.json", {"version": 1, "reason": reason,
                    "files": inventory(stage / "savedata")})
        target = folder / name
        os.replace(stage, target)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    # Never prune a queued restoration or a safety copy made before a restore.
    pending = pending_restore(user)
    rotating = [p for p in snapshots(user) if "-launch-" in p.name and p.name != pending]
    for old in rotating[max(1, keep):]:
        shutil.rmtree(old)
    print(f"Save backup: {target}", flush=True)
    return target


def validated_snapshot(user, name):
    if not name or Path(name).name != name or name.startswith("."):
        raise ValueError("Invalid backup name")
    target = user / "backups" / name
    if target.is_symlink():
        raise ValueError("Backup must not be a symbolic link")
    manifest = json.loads((target / "manifest.json").read_text())
    if manifest.get("version") != 1 or not (target / "savedata").is_dir():
        raise ValueError("Invalid backup manifest")
    if inventory(target / "savedata") != manifest.get("files"):
        raise ValueError("Backup checksum mismatch; current saves have not been changed")
    return target


def pending_restore(user):
    try:
        return json.loads((user / ".restore-request.json").read_text())["snapshot"]
    except FileNotFoundError:
        return None


def recover(user):
    previous = user / ".savedata-previous"
    live = user / "savedata"
    if previous.exists():
        if not live.exists():
            os.replace(previous, live)
        else:
            shutil.rmtree(previous)


def restore_pending(user):
    recover(user)
    name = pending_restore(user)
    if name is None:
        return
    source = validated_snapshot(user, name)
    stage = Path(tempfile.mkdtemp(prefix=".restore-", dir=user))
    try:
        shutil.copytree(source / "savedata", stage / "savedata")
        # Validate the completed copy before touching the current save directory.
        if inventory(stage / "savedata") != inventory(source / "savedata"):
            raise ValueError("Restore copy checksum mismatch")
        backup(user, reason="before-restore")
        live, previous = user / "savedata", user / ".savedata-previous"
        if live.exists():
            os.replace(live, previous)
        try:
            os.replace(stage / "savedata", live)
        except BaseException:
            if previous.exists():
                os.replace(previous, live)
            raise
        (user / ".restore-request.json").unlink()
        recover(user)
        print(f"Restored save: {name}", flush=True)
    finally:
        shutil.rmtree(stage)


def request_restore(user, name):
    validated_snapshot(user, name)
    # Serialize restore requests with their consumption. A running session only queues.
    with (user / ".bbport-restore.lock").open("a+") as request_lock:
        fcntl.flock(request_lock, fcntl.LOCK_EX)
        atomic_json(user / ".restore-request.json", {"snapshot": name})
        try:
            with session_lock(user):
                restore_pending(user)
                return "restored"
        except BlockingIOError:
            return "queued until the game exits"


def consume_restore(user):
    with (user / ".bbport-restore.lock").open("a+") as request_lock:
        fcntl.flock(request_lock, fcntl.LOCK_EX)
        restore_pending(user)


def run(user, command, keep=10):
    # The game's Apply and restart executes run.sh in place. Its original supervisor
    # still owns the save lock; do not open another lock or take a mid-session backup.
    owner = os.environ.get("BB_SAVE_SUPERVISOR", "")
    ancestor = os.getppid()
    while owner and ancestor > 1:
        if str(ancestor) == owner and os.environ.get("BB_SAVE_USER") == str(user):
            os.execvpe(command[0], command, os.environ)
        try:
            ancestor = int(Path(f"/proc/{ancestor}/stat").read_text().rsplit(")", 1)[1].split()[1])
        except (OSError, ValueError, IndexError):
            break
    with session_lock(user) as lock:
        consume_restore(user)
        backup(user, keep=keep)
        child = None
        stopped = 0

        def forward(signum, _frame):
            nonlocal stopped
            stopped = signum
            if child is not None and child.poll() is None:
                child.send_signal(signum)

        handlers = {sig: signal.signal(sig, forward) for sig in (signal.SIGTERM, signal.SIGINT)}
        try:
            # The inherited lock also protects saves if the supervisor alone is killed.
            child = subprocess.Popen(command, pass_fds=(lock.fileno(),),
                env=dict(os.environ, BB_SAVE_SUPERVISOR=str(os.getpid()), BB_SAVE_USER=str(user)))
            if stopped:
                child.send_signal(stopped)
            status = child.wait()
            consume_restore(user)
            return 128 - status if status < 0 else status
        finally:
            for sig, handler in handlers.items():
                signal.signal(sig, handler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--user", type=Path, required=True)
    parser.add_argument("--keep", type=int, default=10)
    parser.add_argument("action", choices=("run", "list", "backup", "restore"))
    parser.add_argument("args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    user = args.user.expanduser().resolve()
    try:
        if args.action == "run":
            command = args.args[1:] if args.args[:1] == ["--"] else args.args
            if not command:
                parser.error("run requires a command")
            return run(user, command, args.keep)
        if args.action == "list":
            for path in snapshots(user):
                print(path.name)
        elif args.action == "restore":
            if len(args.args) != 1:
                parser.error("restore requires a backup name")
            print(request_restore(user, args.args[0]))
        else:
            with session_lock(user):
                backup(user, reason="manual", keep=args.keep)
    except BlockingIOError:
        print("Save directory is in use by another game session.", file=sys.stderr)
        return 1
    except (OSError, ValueError, KeyError) as error:
        print(f"Save operation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
