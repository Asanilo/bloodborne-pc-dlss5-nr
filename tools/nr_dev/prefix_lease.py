"""Move an already retired NR prefix under the current canonical run; never clone it."""
import json
from pathlib import Path

class PrefixLease:
    def __init__(self, source, target, record):
        self.source=Path(source);self.target=Path(target);self.record=Path(record)
        self.started=False;self.returned=False
    def write(self,state):
        self.record.write_text(json.dumps({'source':str(self.source),'target':str(self.target),'state':state})+'\n')
    def __enter__(self):
        for p in (self.source,self.target):
            if p.is_symlink() or p.resolve()!=p or not p.is_dir():raise ValueError('prefix lease requires canonical directories')
        if not (self.source/'system.reg').is_file():raise ValueError('retired NR prefix is not initialized')
        self.target.rmdir() # Must be empty; never replace a populated prefix.
        try:self.source.rename(self.target)
        except BaseException:self.target.mkdir(exist_ok=True);raise
        try:self.write('BORROWED')
        except BaseException:
            self.target.rename(self.source);self.target.mkdir();raise
        return self
    def restore(self):
        if self.source.exists():raise ValueError('prefix source unexpectedly recreated; retained current prefix')
        self.target.rename(self.source);self.target.mkdir();self.returned=True;self.write('RETURNED')
    def complete(self,result):
        if result.get('status')!='PASS_NORMAL_TEARDOWN' or result.get('native_lastuse_proof') is not True or result.get('residuals')!=[]:
            raise ValueError('prefix restore requires actual normal GPU retirement')
        self.restore()
    def __exit__(self,*_):
        if not self.returned and not self.started:self.restore()
        elif not self.returned:self.write('RETAINED_UNPROVED')
