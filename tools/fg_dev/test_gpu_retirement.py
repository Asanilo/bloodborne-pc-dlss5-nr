"""Offline GPU mailbox retirement rejection tests; no actual GPU assertions."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'nr_dev'))
import peer_retirement as audit
import dlssg_backend

PROOF='DLSSG_GPU_BRIDGE_RETIRED helper_GPU_complete=1 feature_released=1 child_reaped=1 Native_shared_GPU_complete=1 imports_destroyed=1 sequence=6\n'

class GpuRetirementTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        base={'schema':'dlssg-gpu-mailbox-v1','session':'test'}
        pair=base|{'sequence':6,'epoch':1,'source_game_frame':106,'previous_source_game_frame':105,'display':123}
        terminal=pair|dict(actual_GPU_complete=True,feature_released=True,device_destroyed=True,native_imports_released=True,
                          shared_images_owned=0,export_raw_fd_owned=0,native_gpu_readers=0,helper_serial=7,native_last_use_tick=20)
        resources=[dict(role=role,format=fmt,width=w,height=h,dedicated=True,offset=0,fd=i+10)
                   for i,(role,fmt,w,h) in enumerate([('color',37,1920,1080),('depth',100,1280,720),('motion',83,1280,720),('output',37,1920,1080)])]
        meta=base|dict(resources=resources,completed_GPU=True,initial_release_serial=1)
        closed=base|dict(export_raw_fd_owned=0,closed_fd=[dict(role=r['role'],fd=r['fd']) for r in resources])
        self.docs={'dlssg-launch.json':base|{'launcher_pid':1234},
                   'dlssg-retired.json':dict(terminal=terminal,launcher_pid=1234,wait_status=0,Native_shared_GPU_complete=True,
                        Native_imports_destroyed=True,native_shared_used=True,native_last_use_tick=20,last_native_input_tick=19,last_request_pair=pair),
                   'dlssg-gpu-import.json':base|dict(metadata=meta,FD_retired=closed,raw_payloads_absent=True)}
    def check(self,docs=None,proof=PROOF):
        for name,data in (docs or self.docs).items():(self.root/name).write_text(json.dumps(data))
        audit.audit_dlssg_gpu(self.root,'test',proof)
    def test_normal_tuple_and_zero_requests(self):
        self.check()
        d=copy.deepcopy(self.docs);r=d['dlssg-retired.json']
        for key in ('sequence','epoch','source_game_frame','previous_source_game_frame','display'):
            r['last_request_pair'][key]=0;r['terminal'][key]=0
        r['terminal']['helper_serial']=1;r['last_native_input_tick']=0
        # A partially recorded producer without any Evaluate still has actual final use.
        self.check(d,PROOF.replace('sequence=6','sequence=0'))
        r['native_shared_used']=False;r['native_last_use_tick']=r['terminal']['native_last_use_tick']=0
        self.check(d,PROOF.replace('sequence=6','sequence=0'))
    def test_missing_or_mismatched_gpu_retirement_facts_reject(self):
        mutations=[('dlssg-retired.json',('Native_shared_GPU_complete',),False),
                   ('dlssg-retired.json',('Native_imports_destroyed',),False),
                   ('dlssg-retired.json',('wait_status',),1),
                   ('dlssg-retired.json',('last_native_input_tick',),20),
                   ('dlssg-retired.json',('native_last_use_tick',),20.0),
                   ('dlssg-retired.json',('terminal','source_game_frame'),107),
                   ('dlssg-retired.json',('terminal','helper_serial'),6),
                   ('dlssg-retired.json',('terminal','shared_images_owned'),1),
                   ('dlssg-retired.json',('terminal','native_imports_released'),False),
                   ('dlssg-gpu-import.json',('raw_payloads_absent',),False),
                   ('dlssg-gpu-import.json',('FD_retired','session'),'old'),
                   ('dlssg-gpu-import.json',('FD_retired','closed_fd'),[])]
        for name,keys,value in mutations:
            with self.subTest(keys=keys):
                d=copy.deepcopy(self.docs);target=d[name]
                for key in keys[:-1]:target=target[key]
                target[keys[-1]]=value
                with self.assertRaises(ValueError):self.check(d)
        with self.assertRaises(ValueError):self.check(proof=PROOF+PROOF)
        with self.assertRaises(ValueError):self.check(proof=PROOF.replace('GPU_BRIDGE','CPU_BRIDGE'))
    def test_sealed_gpu_transport_cannot_select_cpu_backend(self):
        path=self.root/'dlssg-config.json'
        cfg=dict(schema='dlssg-native-gpu-v1',session='test',run=str(self.root),ipc='/tmp/bb-dlssg-test',transport='gpu-dmabuf-v1')
        path.write_text(json.dumps(cfg))
        env=dict(BB_FRAME_GEN_BACKEND='dlssg',BB_FRAME_GEN_NATIVE='1',BB_DLSSG_TRANSPORT='gpu-dmabuf-v1',
                 BB_DLSSG_STREAM_CONFIG=str(path),BB_DLSSG_STREAM_DIR=cfg['ipc'],BB_DLSSG_CONFIG_SHA256=hashlib.sha256(path.read_bytes()).hexdigest(),
                 BB_NR_NATIVE_ORIGIN_PREFIX=str(self.root/'profile/wine-prefix'))
        manifest=dict(session='test',environment=env);dlssg_backend.validate_manifest(self.root,manifest)
        for transport in ('mapped-v1',None):
            env['BB_DLSSG_TRANSPORT']=transport
            with self.assertRaisesRegex(ValueError,'transport drift'):dlssg_backend.validate_manifest(self.root,manifest)

if __name__=='__main__':unittest.main()
