"""One per-UID boot receipt, shared by all NRBridge homes.

Call admission/begin/finish while holding the existing exclusive project GPU lock.
Pending and HOLD receipts are never absence/retirement evidence. The runtime directory
argument exists for CPU fixtures; production always uses /run/user/<actual UID>.
"""
import json
import os
from pathlib import Path
import re
import stat
import uuid

SCHEMA = 'nrbridge-boot-session-v1'
NAME = 'bbport-nrbridge-session.json'
# A current-boot HOLD can be re-admitted once, by an explicit recovery record:
# a short GPU submit probe passed and the kernel journal shows no driver error.
RECOVERY_NAME = 'bbport-nrbridge-recovery.json'
RECOVERY_SCHEMA = 'nrbridge-recovery-v1'
RECOVERY_KEYS = {'schema', 'uid', 'boot_id', 'session', 'probe', 'receipt',
                 'receipt_sha256', 'journal_clean_after_probe', 'created_ns', 'nonce'}
PROBE_KEYS = {'tool', 'sha256', 'result', 'output_sha256'}
MAX_BYTES = 8192
KEYS = {'schema', 'uid', 'boot_id', 'state', 'root', 'session', 'nonce'}


class SessionGuard:
    def __init__(self, runtime_root=None, boot_id=None):
        self.uid = os.getuid()
        self.directory = Path(runtime_root) if runtime_root is not None else Path('/run/user') / str(self.uid)
        self.boot_id = boot_id or Path('/proc/sys/kernel/random/boot_id').read_text().strip()
        self._boot(self.boot_id)
        self.path = self.directory / NAME
        self._directory()

    @staticmethod
    def _boot(value):
        if not isinstance(value, str) or str(uuid.UUID(value)) != value:
            raise ValueError('boot receipt requires a canonical boot UUID')

    def _directory(self):
        path = self.directory
        if not path.is_absolute() or path.resolve() != path or path.is_symlink():
            raise ValueError('boot receipt runtime directory must be canonical')
        info = path.stat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != self.uid or stat.S_IMODE(info.st_mode) != 0o700:
            raise ValueError('boot receipt runtime directory requires actual UID ownership and mode 0700')

    def _validate(self, record):
        if not isinstance(record, dict) or set(record) != KEYS or record.get('schema') != SCHEMA:
            raise ValueError('malformed boot session receipt')
        if type(record['uid']) is not int or record['uid'] != self.uid:
            raise ValueError('boot session receipt UID mismatch')
        self._boot(record['boot_id'])
        if not isinstance(record['state'], str) or record['state'] not in ('pending', 'hold', 'retired'):
            raise ValueError('invalid boot session receipt state')
        root = record['root']
        if (not isinstance(root, str) or not root or len(root) > 2048
                or any(c in root for c in ('\x00', '\n', ':'))):
            raise ValueError('unsafe root in boot session receipt')
        path = Path(root)
        if not path.is_absolute() or str(path) != root or path.resolve() != path or path == Path('/'):
            raise ValueError('noncanonical root in boot session receipt')
        if not isinstance(record['session'], str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9-]{0,62}', record['session']):
            raise ValueError('unsafe session in boot session receipt')
        if not isinstance(record['nonce'], str) or not re.fullmatch(r'[0-9a-f]{32}', record['nonce']):
            raise ValueError('invalid boot session receipt nonce')
        return record

    def read(self):
        self._directory()
        try:
            descriptor = os.open(self.path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        except FileNotFoundError:
            # First admission in this boot may have no receipt. Retirement never accepts absence.
            if self.path.is_symlink():
                raise ValueError('boot session receipt symlink refused') from None
            return None
        except OSError:
            raise ValueError('boot session receipt unreadable or unsafe') from None
        try:
            before = os.fstat(descriptor)
            if (not stat.S_ISREG(before.st_mode) or before.st_uid != self.uid
                    or stat.S_IMODE(before.st_mode) != 0o600 or before.st_nlink != 1
                    or not 0 < before.st_size <= MAX_BYTES):
                raise ValueError('boot session receipt requires bounded UID-owned regular file mode 0600')
            data = os.read(descriptor, MAX_BYTES + 1)
            after = os.fstat(descriptor)
            identity = lambda value: (value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns,
                                      value.st_nlink, value.st_uid, value.st_mode)
            if (len(data) != before.st_size or identity(before) != identity(after)):
                raise ValueError('boot session receipt changed while reading')
        finally:
            os.close(descriptor)
        def unique(pairs):
            result = {}
            for key, value in pairs:
                if key in result:
                    raise ValueError('duplicate boot session receipt field')
                result[key] = value
            return result
        try:
            record = json.loads(data, object_pairs_hook=unique)
        except (ValueError, UnicodeError):
            raise ValueError('malformed boot session receipt JSON') from None
        return self._validate(record)

    def admit(self):
        record = self.read()
        if record is not None and record['boot_id'] == self.boot_id and record['state'] != 'retired':
            if not self._recovered(record):
                raise ValueError('current-boot global session pending/HOLD: ' + record['root'] + '/runs/' + record['session'])
        return record

    def _recovery_path(self):
        return self.directory / RECOVERY_NAME

    def read_recovery(self):
        path = self._recovery_path()
        try:
            descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        except FileNotFoundError:
            if path.is_symlink():
                raise ValueError('recovery record symlink refused') from None
            return None
        except OSError:
            raise ValueError('recovery record unreadable or unsafe') from None
        try:
            before = os.fstat(descriptor)
            if (not stat.S_ISREG(before.st_mode) or before.st_uid != self.uid
                    or stat.S_IMODE(before.st_mode) != 0o600 or before.st_nlink != 1
                    or not 0 < before.st_size <= MAX_BYTES):
                raise ValueError('recovery record requires bounded UID-owned regular file mode 0600')
            data = os.read(descriptor, MAX_BYTES + 1)
            after = os.fstat(descriptor)
            if len(data) != before.st_size or (before.st_dev, before.st_ino, before.st_mtime_ns) != (
                    after.st_dev, after.st_ino, after.st_mtime_ns):
                raise ValueError('recovery record changed while reading')
        finally:
            os.close(descriptor)
        def unique(pairs):
            result = {}
            for key, value in pairs:
                if key in result:
                    raise ValueError('duplicate recovery record field')
                result[key] = value
            return result
        try:
            record = json.loads(data, object_pairs_hook=unique)
        except (ValueError, UnicodeError):
            raise ValueError('malformed recovery record JSON') from None
        self._validate_recovery(record)
        return record

    def _validate_recovery(self, record):
        if not isinstance(record, dict) or set(record) != RECOVERY_KEYS:
            raise ValueError('malformed recovery record')
        if record.get('schema') != RECOVERY_SCHEMA or type(record.get('uid')) is not int or record['uid'] != self.uid:
            raise ValueError('recovery record UID/schema mismatch')
        self._boot(record.get('boot_id'))
        if record['boot_id'] != self.boot_id:
            raise ValueError('recovery record belongs to another boot')
        if not isinstance(record.get('session'), str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9-]{0,62}', record['session']):
            raise ValueError('unsafe recovery session')
        probe = record.get('probe')
        if not isinstance(probe, dict) or set(probe) != PROBE_KEYS:
            raise ValueError('malformed recovery probe')
        if not isinstance(probe['tool'], str) or not probe['tool']:
            raise ValueError('recovery probe tool required')
        if not isinstance(probe['sha256'], str) or not re.fullmatch(r'[0-9a-f]{64}', probe['sha256']):
            raise ValueError('recovery probe binary SHA required')
        if not isinstance(probe['output_sha256'], str) or not re.fullmatch(r'[0-9a-f]{64}', probe['output_sha256']):
            raise ValueError('recovery probe output SHA required')
        if probe['result'] != 'PASS' or record.get('journal_clean_after_probe') is not True:
            raise ValueError('recovery requires a passing probe and a clean journal')
        receipt = record.get('receipt')
        self._validate(receipt)
        if not isinstance(record.get('receipt_sha256'), str) or not re.fullmatch(r'[0-9a-f]{64}', record['receipt_sha256']):
            raise ValueError('recovery receipt SHA required')
        if type(record.get('created_ns')) is not int or record['created_ns'] <= 0:
            raise ValueError('recovery timestamp required')
        if not isinstance(record.get('nonce'), str) or not re.fullmatch(r'[0-9a-f]{32}', record['nonce']):
            raise ValueError('invalid recovery nonce')

    def _recovered(self, receipt):
        record = self.read_recovery()
        if record is None:
            return False
        return record['boot_id'] == self.boot_id and record['session'] == receipt['session']

    def write_recovery(self, receipt, probe):
        # Only a current-boot pending/HOLD receipt can be recovered; the failed
        # session's own record stays HOLD and is embedded unchanged.
        current = self.read()
        self._validate(receipt)
        if current != receipt or receipt['boot_id'] != self.boot_id or receipt['state'] == 'retired':
            raise ValueError('recovery requires the current non-retired boot receipt')
        if not isinstance(probe, dict) or set(probe) != PROBE_KEYS or probe.get('result') != 'PASS':
            raise ValueError('recovery requires a passing probe record')
        payload = json.dumps(receipt, sort_keys=True).encode()
        record = {'schema': RECOVERY_SCHEMA, 'uid': self.uid, 'boot_id': self.boot_id,
                  'session': receipt['session'], 'probe': probe, 'receipt': receipt,
                  'receipt_sha256': __import__('hashlib').sha256(payload).hexdigest(),
                  'journal_clean_after_probe': True,
                  'created_ns': __import__('time').time_ns(),
                  'nonce': uuid.uuid4().hex}
        self._validate_recovery(record)
        data = (json.dumps(record, sort_keys=True) + chr(10)).encode()
        if len(data) > MAX_BYTES:
            raise ValueError('recovery record too large')
        temporary = self.directory / (RECOVERY_NAME + '.' + uuid.uuid4().hex + '.tmp')
        descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
        try:
            with os.fdopen(descriptor, 'wb') as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, self._recovery_path())
            directory_fd = os.open(self.directory, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
        finally:
            temporary.unlink(missing_ok=True)
        return self._recovery_path()

    def _write(self, record):
        self._directory()
        self._validate(record)
        data = (json.dumps(record, sort_keys=True) + '\n').encode()
        if len(data) > MAX_BYTES:
            raise ValueError('boot session receipt too large')
        temporary = self.directory / (NAME + '.' + uuid.uuid4().hex + '.tmp')
        descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
        try:
            with os.fdopen(descriptor, 'wb') as stream:
                os.fchmod(stream.fileno(), 0o600)
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            # Even under the project lock, never replace an unsafe/unreadable existing file.
            self.read()
            os.replace(temporary, self.path)
            directory_fd = os.open(self.directory, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
        finally:
            temporary.unlink(missing_ok=True)

    def begin(self, root, session):
        self.admit()
        record = {'schema': SCHEMA, 'uid': self.uid, 'boot_id': self.boot_id, 'state': 'pending',
                  'root': str(root), 'session': session, 'nonce': uuid.uuid4().hex}
        self._write(record)
        return record

    def _own_pending(self, receipt):
        self._validate(receipt)
        if receipt['boot_id'] != self.boot_id or receipt['state'] != 'pending' or self.read() != receipt:
            raise ValueError('matching own pending boot session receipt required')

    def hold(self, receipt):
        self._own_pending(receipt)
        self._write(receipt | {'state': 'hold'})

    def retire(self, receipt, result):
        if (result.get('status') != 'PASS_NORMAL_TEARDOWN' or type(result.get('returncode')) is not int
                or result['returncode'] != 0 or result.get('native_lastuse_proof') is not True
                or result.get('residuals') != [] or result.get('watchdog_expired') is not False
                or result.get('supervisor_interrupted') is not False):
            raise ValueError('actual normal GPU retirement proof required for boot receipt')
        self._own_pending(receipt)
        self._write(receipt | {'state': 'retired'})

    def retire_reaudited(self, receipt, run):
        # Ordinary retire() still rejects every HOLD. This separate correction
        # requires the original error, every actual owner proof, and exact nonce.
        self._validate(receipt)
        if (receipt['boot_id'] != self.boot_id or receipt['state'] != 'hold'
                or self.read() != receipt):
            raise ValueError('matching own held receipt required for reaudit')
        from retirement_reaudit import validate_reconciliation
        validate_reconciliation(Path(run), receipt)
        if self.read() != receipt:
            raise ValueError('held receipt changed during actual owner reaudit')
        self._write(receipt | {'state': 'retired'})
