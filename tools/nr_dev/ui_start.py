"""In-game scene confirmation. Never a Helper/GPU readiness or retirement proof."""
import os
import re
import stat
from pathlib import Path

CAPABILITY = b'NRBRIDGE_NATIVE_INGAME_START_V1'
ENVIRONMENT = 'BB_NR_UI_START_REQUEST'
NAME = 'nr-ui-start-request'


def capable(build):
    try:
        return CAPABILITY in (Path(build) / 'gpu/libbbgpu.so').read_bytes()
    except OSError:
        return False


def enabled(args, build):
    return getattr(args, 'daily', False) is True and not getattr(args, 'sr_only', False) and capable(build)


def validate(run, manifest):
    run = Path(run)
    environment = manifest.get('environment', {})
    if (run.resolve() != run or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9-]{0,62}', run.name)
            or manifest.get('session') != run.name or manifest.get('daily') is not True
            or manifest.get('nr_enabled') is not True or environment.get('BB_NR_DAILY') != '1'
            or environment.get('BB_NR_NATIVE_ASYNC_RUN_DIR') != str(run)
            or environment.get('BB_NR_NATIVE_ASYNC_SESSION') != run.name
            or environment.get(ENVIRONMENT) != str(run / NAME)
            or not capable(manifest.get('native_build', ''))):
        raise ValueError('in-game NR scene confirmation scope drift')


def requested(run, manifest):
    """Read only the exact private run's bounded, same-session player intent."""
    validate(run, manifest)
    run = Path(run)
    # The writer walks the same canonical directory without following links.
    # Pin that directory before opening the marker; do not open a FIFO/device.
    directory = os.open('/', os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC)
    try:
        for part in run.parts[1:]:
            next_directory = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC | os.O_NOFOLLOW,
                                     dir_fd=directory)
            os.close(directory)
            directory = next_directory
        try:
            fd = os.open(NAME, os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC | os.O_NOFOLLOW, dir_fd=directory)
        except FileNotFoundError:
            return False
        try:
            before = os.fstat(fd)
            if (not stat.S_ISREG(before.st_mode) or before.st_uid != os.getuid()
                    or stat.S_IMODE(before.st_mode) != 0o600 or before.st_nlink != 1):
                raise ValueError('in-game NR request requires a private regular file')
            token = (run.name + '\n').encode('ascii')
            data = os.read(fd, 66)
            after = os.fstat(fd)
            signature = lambda info: (info.st_dev, info.st_ino, info.st_mode, info.st_uid,
                                       info.st_nlink, info.st_size, info.st_mtime_ns, info.st_ctime_ns)
            if signature(before) != signature(after):
                return False  # Publication still in progress; never infer readiness.
            if len(data) < len(token) and token.startswith(data):
                return False
            if data != token or after.st_size != len(token):
                raise ValueError('in-game NR request session/content drift')
            return True
        finally:
            os.close(fd)
    finally:
        os.close(directory)
