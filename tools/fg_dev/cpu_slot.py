"""Fixed 1080p Quality CPU payload. No Vulkan, Wine or GPU operations."""
import contextlib
import mmap
import os
from pathlib import Path
import stat

COLOR_BYTES=1920*1080*4
GUIDE_BYTES=1280*720*4
OFFSETS={'color':(0,COLOR_BYTES),'depth':(COLOR_BYTES,GUIDE_BYTES),
         'motion':(COLOR_BYTES+GUIDE_BYTES,GUIDE_BYTES),
         'output':(COLOR_BYTES+2*GUIDE_BYTES,COLOR_BYTES)}
SLOT_BYTES=2*COLOR_BYTES+2*GUIDE_BYTES


@contextlib.contextmanager
def mapping(root,create=False):
    path=Path(root)/'slot.bin'
    fd=os.open(path,os.O_RDWR|os.O_NOFOLLOW|os.O_CLOEXEC|(os.O_CREAT|os.O_EXCL if create else 0),0o600)
    try:
        if create:os.ftruncate(fd,SLOT_BYTES)
        st=os.fstat(fd)
        if not (stat.S_ISREG(st.st_mode) and st.st_uid==os.getuid() and
                stat.S_IMODE(st.st_mode)==0o600 and st.st_nlink==1 and st.st_size==SLOT_BYTES):
            raise ValueError('exact private CPU payload length/mode required')
        with mmap.mmap(fd,SLOT_BYTES,access=mmap.ACCESS_WRITE) as payload:
            yield payload
    finally:
        os.close(fd)
