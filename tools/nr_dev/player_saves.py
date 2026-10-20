"""Bounded private-session save copies under the original launcher's save lock."""
from contextlib import contextmanager
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import tempfile

_spec = importlib.util.spec_from_file_location(
    'nrbridge_save_manager', Path(__file__).resolve().parents[2] / 'scripts/save_manager.py')
save_manager = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(save_manager)
MAX_ENTRIES = 8192
MAX_BYTES = 2 * 1024**3


def _canonical(path):
    path = Path(path)
    if not path.is_absolute() or path != path.resolve():
        raise ValueError('Save path must be absolute and canonical, without symlinks')
    return path


def _tree(root):
    """Reject links/special files before hashing; distinguish absent and empty saves."""
    if not root.exists() and not root.is_symlink():
        return {'exists': False, 'files': {}, 'directories': []}
    if root.is_symlink() or not root.is_dir():
        raise ValueError(f'Save directory must be a real directory: {root}')
    count = size = 0
    directories = []
    pending = [root]
    while pending:
        directory = pending.pop()
        with os.scandir(directory) as entries:
            for entry in entries:
                count += 1
                if count > MAX_ENTRIES:
                    raise ValueError('Save entry limit exceeded')
                info = entry.stat(follow_symlinks=False)
                path = Path(entry.path)
                if stat.S_ISDIR(info.st_mode):
                    directories.append(str(path.relative_to(root)))
                    pending.append(path)
                elif stat.S_ISREG(info.st_mode):
                    size += info.st_size
                    if size > MAX_BYTES:
                        raise ValueError('Save byte limit exceeded')
                else:
                    raise ValueError(f'Unsupported save entry: {path}')
    return {'exists': True, 'files': save_manager.inventory(root),
            'directories': sorted(directories)}


def _digest(tree):
    return hashlib.sha256(json.dumps(tree, sort_keys=True).encode()).hexdigest()


def _copy_verified(source, target, expected):
    if expected['exists']:
        # Copy links as links so a raced-in link cannot be followed into unrelated data.
        shutil.copytree(source, target, symlinks=True)
    else:
        target.mkdir()
    copied = _tree(target)
    wanted = dict(expected, exists=True)
    if copied != wanted or _tree(source) != expected:
        raise ValueError('Save copy checksum/source stability mismatch')


class PlayerSaves:
    def __init__(self, user: Path):
        self.user = _canonical(user)
        self._lock = None
        self._baseline = None
        self._profile = None
        self._committed = False

    def _controls(self):
        for name in ('.bbport-session.lock', '.bbport-restore.lock', '.restore-request.json'):
            path = self.user / name
            if path.is_symlink() or (path.exists() and not path.is_file()):
                raise ValueError(f'Invalid save control file: {path}')
        for name in ('backups', '.savedata-previous'):
            path = self.user / name
            if path.is_symlink() or (path.exists() and not path.is_dir()):
                raise ValueError(f'Invalid save control directory: {path}')

    @contextmanager
    def _restore_lock(self):
        self._controls()
        with (self.user / '.bbport-restore.lock').open('a+') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            yield

    def _restore_pending(self):
        # Called only with both shared locks held; validate queued trees before the
        # original helper copies or recovers them.
        _tree(self.user / '.savedata-previous')
        name = save_manager.pending_restore(self.user)
        if name is not None:
            if not isinstance(name, str) or Path(name).name != name or name.startswith('.'):
                raise ValueError('Invalid backup name')
            snapshot = _canonical(self.user / 'backups' / name)
            _tree(snapshot / 'savedata')
        save_manager.restore_pending(self.user)

    def _require_open(self):
        if self._lock is None:
            raise ValueError('Save transaction is not open')

    def __enter__(self):
        if self._lock is not None:
            raise ValueError('Save transaction is already open')
        _canonical(self.user)
        self.user.mkdir(parents=True, exist_ok=True)
        if not self.user.is_dir():
            raise ValueError('Player user must be a directory')
        self._controls()
        self._lock = save_manager.session_lock(self.user)
        try:
            self._lock.__enter__()
            with self._restore_lock():
                _tree(self.user / 'savedata')
                self._restore_pending()
                _tree(self.user / 'savedata')
                save_manager.backup(self.user, reason='launch')
        except BaseException:
            self.__exit__(None, None, None)
            raise
        return self

    def __exit__(self, *exc):
        lock, self._lock = self._lock, None
        if lock is not None:
            lock.__exit__(*exc)

    def prepare(self, profile_user: Path) -> dict:
        self._require_open()
        if self._baseline is not None:
            raise ValueError('Save transaction is already prepared')
        profile = _canonical(profile_user)
        if profile == self.user or self.user in profile.parents or profile in self.user.parents:
            raise ValueError('Private save profile must be separate from player user')
        profile.mkdir(parents=True, exist_ok=True)
        target = profile / 'savedata'
        if target.exists() or target.is_symlink():
            raise ValueError('Private savedata must not already exist')
        baseline = _tree(self.user / 'savedata')
        _copy_verified(self.user / 'savedata', target, baseline)
        self._baseline, self._profile = baseline, profile
        return {'source': str(self.user / 'savedata'), 'source_inventory_hash': _digest(baseline),
                'source_files': baseline['files']}

    def commit(self, profile_user: Path, result: dict) -> dict:
        """Save errors remain separate from the supervisor's GPU retirement result."""
        stage = None
        swapped = False
        try:
            self._require_open()
            if self._baseline is None or _canonical(profile_user) != self._profile:
                raise ValueError('Save transaction/profile was not prepared')
            if self._committed:
                raise ValueError('Save transaction has already committed')
            proven = (result.get('status') == 'PASS_NORMAL_TEARDOWN'
                      and type(result.get('returncode')) is int and result['returncode'] == 0
                      and result.get('native_lastuse_proof') is True
                      and result.get('residuals') == []
                      and result.get('watchdog_expired') is False
                      and result.get('supervisor_interrupted') is False)
            if not proven:
                return {'status': 'retained', 'reason': 'normal teardown proof required',
                        'private_savedata': str(self._profile / 'savedata')}
            live = self.user / 'savedata'
            if _tree(live) != self._baseline:
                raise ValueError('Player saves changed externally; private saves retained')
            candidate = self._profile / 'savedata'
            expected = _tree(candidate)
            if not expected['exists']:
                raise ValueError('Private savedata is missing')
            stage = Path(tempfile.mkdtemp(prefix='.nrbridge-sync-', dir=self.user))
            _copy_verified(candidate, stage / 'savedata', expected)
            with self._restore_lock():
                if _tree(live) != self._baseline:
                    raise ValueError('Player saves changed externally; private saves retained')
                backup = save_manager.backup(self.user, reason='before-nrbridge-sync')
                if _tree(live) != self._baseline:
                    raise ValueError('Player saves changed during backup; private saves retained')
                previous = self.user / '.savedata-previous'
                if previous.exists():
                    raise ValueError('Unrecovered previous savedata; private saves retained')
                had_live = live.exists()
                if had_live:
                    os.replace(live, previous)
                try:
                    os.replace(stage / 'savedata', live)
                except BaseException as swap_error:
                    if had_live:
                        try:
                            os.replace(previous, live)
                        except Exception as rollback_error:
                            raise RuntimeError(
                                f'Swap failed ({swap_error}); rollback failed ({rollback_error}); '
                                f'original saves retained at {previous}, backup {backup}'
                            ) from rollback_error
                    raise
                swapped = self._committed = True
                save_manager.recover(self.user)
                # Queue consumption shares the original restore-request lock.
                self._restore_pending()
            return {'status': 'synced', 'source': str(live),
                    'source_inventory_hash': _digest(_tree(live)),
                    'backup': str(backup) if backup else None}
        except Exception as error:
            return {'status': 'error', 'error': f'{type(error).__name__}: {error}',
                    'synced_before_error': swapped,
                    'private_savedata': str(Path(profile_user) / 'savedata')}
        finally:
            if stage is not None:
                shutil.rmtree(stage, ignore_errors=True)
