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
HELPER_PROOF='FG_POC_RETIRED actual_GPU_complete=1 feature_released=1 core_shutdown=1 device_destroyed=1 native_readers=0\n'

class GpuRetirementTests(unittest.TestCase):
    def test_pipeline_is_sealed_and_old_configs_keep_sync(self):
        path=self.root/'dlssg-config.json'
        cfg=dict(schema='dlssg-native-gpu-v1',session='test',run=str(self.root),
                 ipc='/tmp/bb-dlssg-test',transport='gpu-dmabuf-v1')
        def manifest(pipeline=None):
            if pipeline is not None:cfg['pipeline']=pipeline
            path.write_text(json.dumps(cfg))
            return dict(session='test',environment=dict(BB_FRAME_GEN_BACKEND='dlssg',
                BB_FRAME_GEN_NATIVE='1',BB_DLSSG_TRANSPORT='gpu-dmabuf-v1',
                BB_DLSSG_STREAM_CONFIG=str(path),BB_DLSSG_STREAM_DIR=cfg['ipc'],
                BB_DLSSG_CONFIG_SHA256=hashlib.sha256(path.read_bytes()).hexdigest(),
                BB_NR_NATIVE_ORIGIN_PREFIX=str(self.root/'profile/wine-prefix')))
        old=manifest();dlssg_backend.validate_manifest(self.root,old)
        new=manifest('submit-ready-v1')
        with self.assertRaisesRegex(ValueError,'pipeline drift'):
            dlssg_backend.validate_manifest(self.root,new)
        new['environment']['BB_DLSSG_PIPELINE']='submit-ready-v1'
        dlssg_backend.validate_manifest(self.root,new)

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
        self.docs={'dlssg-config.json':{'multiplier':2},'dlssg-launch.json':base|{'launcher_pid':1234},
                   'dlssg-retired.json':dict(terminal=terminal,launcher_pid=1234,wait_status=0,Native_shared_GPU_complete=True,
                        Native_imports_destroyed=True,native_shared_used=True,native_last_use_tick=20,last_native_input_tick=19,last_request_pair=pair),
                   'dlssg-gpu-import.json':base|dict(metadata=meta,FD_retired=closed,raw_payloads_absent=True)}
    def check(self,docs=None,proof=PROOF,split=False):
        for name,data in (docs or self.docs).items():(self.root/name).write_text(json.dumps(data))
        audit.audit_dlssg_gpu(self.root,'test',proof,split)
    def test_dynamic_generations_are_all_retired_and_exact(self):
        ledger={'schema':'dlssg-layout-generations-v1','session':'test','active':0,'entries':[]}
        layout=dict(revision=1,render_width=1706,render_height=960,output_width=2560,output_height=1440,color_format=37,depth_format=100,motion_format=83,source_rect_origin=[0,0],color_domain='finished-scene-bytes')
        for n in (1,2):
            directory=f'dlssg-generations/g{n}';target=self.root/directory;target.mkdir(parents=True)
            docs=copy.deepcopy(self.docs);docs['dlssg-config.json'].update(layout=layout,resource_epoch=n)
            report=docs['dlssg-retired.json'];report['last_request_pair'].update(layout=layout,resource_epoch=n);report['terminal'].update(layout=layout,resource_epoch=n)
            meta=docs['dlssg-gpu-import.json']['metadata'];meta.update(layout=layout,resource_epoch=n)
            for r in meta['resources']:
                r.update(width=1706 if r['role'] in ('depth','motion') else 2560,height=960 if r['role'] in ('depth','motion') else 1440)
            for name,data in docs.items():(target/name).write_text(json.dumps(data))
            (target/'native-proof.log').write_text(PROOF)
            (target/'dlssg-helper.log').write_text(HELPER_PROOF)
            ledger['entries'].append(dict(resource_epoch=n,directory=directory,layout=layout))
        path=self.root/'dlssg-generations.json';path.write_text(json.dumps(ledger));audit.audit_dlssg_gpu(self.root,'test',PROOF)
        manifest=dict(session='test',environment={'BB_DLSSG_TRANSPORT':'gpu-dmabuf-v1'})
        audit.audit_dlssg_owners(self.root,manifest,PROOF)
        # Exercise the actual owner dispatch, not just its recursive leaf audit.
        helper=self.root/'dlssg-generations/g2/dlssg-helper.log';helper.write_text('')
        with self.assertRaisesRegex(ValueError,'unique Helper'):audit.audit_dlssg_owners(self.root,manifest,PROOF)
        helper.write_text(HELPER_PROOF)
        legacy=self.root/'dlssg-launch.json';legacy.write_text('{}')
        with self.assertRaisesRegex(ValueError,'cannot coexist'):audit.audit_dlssg_owners(self.root,manifest,PROOF)
        legacy.unlink()
        with self.assertRaisesRegex(ValueError,'GPU generation ledger'):
            audit.audit_dlssg_owners(self.root,dict(session='test',environment={'BB_DLSSG_TRANSPORT':'mapped-v1'}),PROOF)
        ledger['active']=2;path.write_text(json.dumps(ledger))
        with self.assertRaises(ValueError):audit.audit_dlssg_gpu(self.root,'test',PROOF)
        ledger['active']=0;path.write_text(json.dumps(ledger))
        target=self.root/'dlssg-generations/g1/dlssg-retired.json';report=json.loads(target.read_text());report['terminal']['resource_epoch']=2;target.write_text(json.dumps(report))
        with self.assertRaises(ValueError):audit.audit_dlssg_gpu(self.root,'test',PROOF)

    def test_orphan_generation_cannot_be_reported_as_no_helper(self):
        (self.root/'dlssg-generations').mkdir()
        with self.assertRaisesRegex(ValueError,'GPU generation ledger'):
            audit.audit_dlssg_owners(self.root,dict(session='test',environment={'BB_DLSSG_TRANSPORT':'gpu-dmabuf-v1'}),
                                     'DLSSG_GPU_BRIDGE_NO_PEER no_helper_created=1\n')

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
    def test_split_domains_and_partial_producer_retirement(self):
        d=copy.deepcopy(self.docs);r=d['dlssg-retired.json'];t=r['terminal'];pair=r['last_request_pair']
        for obj in (pair,t):obj.update(native_completion_mode='split-v1',native_input_domain='draw')
        for obj in (r,t):obj.update(native_completion_mode='split-v1',native_last_use_domain='consumer',native_last_use_tick=2,
                                   native_draw_last_use_tick=19,native_draw_GPU_complete=True,native_consumer_GPU_complete=True)
        self.check(d,split=True) # Draw 19 and consumer 2 are independent counters.
        with self.assertRaises(ValueError):self.check(d) # Sealed legacy mode cannot admit split evidence.
        bad=copy.deepcopy(d);bad['dlssg-retired.json']['native_completion_mode']='unknown-v9'
        with self.assertRaises(ValueError):self.check(bad,split=True)
        for key,value in [('native_draw_last_use_tick',18),('native_consumer_GPU_complete',False),('native_last_use_domain','draw')]:
            bad=copy.deepcopy(d);bad['dlssg-retired.json'][key]=value
            with self.assertRaises(ValueError):self.check(bad,split=True)
        for obj in (pair,t):
            for key in ('sequence','epoch','source_game_frame','previous_source_game_frame','display'):obj[key]=0
        t['helper_serial']=1;r['last_native_input_tick']=0
        r['native_last_use_tick']=t['native_last_use_tick']=0
        self.check(d,PROOF.replace('sequence=6','sequence=0'),split=True) # Partial guides, no consumer use.
        r['native_last_use_tick']=t['native_last_use_tick']=1
        with self.assertRaises(ValueError):self.check(d,PROOF.replace('sequence=6','sequence=0'),split=True)

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

    def test_helper_wake_is_sealed_and_legacy_defaults_to_poll(self):
        path=self.root/'dlssg-config.json'
        cfg=dict(schema='dlssg-native-gpu-v1',session='test',run=str(self.root),ipc='/tmp/bb-dlssg-test',transport='gpu-dmabuf-v1',helper_wake='notify-v1')
        path.write_text(json.dumps(cfg))
        env=dict(BB_FRAME_GEN_BACKEND='dlssg',BB_FRAME_GEN_NATIVE='1',BB_DLSSG_TRANSPORT='gpu-dmabuf-v1',BB_DLSSG_HELPER_WAKE='notify-v1',
                 BB_DLSSG_STREAM_CONFIG=str(path),BB_DLSSG_STREAM_DIR=cfg['ipc'],BB_DLSSG_CONFIG_SHA256=hashlib.sha256(path.read_bytes()).hexdigest(),
                 BB_NR_NATIVE_ORIGIN_PREFIX=str(self.root/'profile/wine-prefix'))
        manifest=dict(session='test',environment=env);dlssg_backend.validate_manifest(self.root,manifest)
        env['BB_DLSSG_HELPER_WAKE']='poll-v1'
        with self.assertRaisesRegex(ValueError,'Helper wake drift'):dlssg_backend.validate_manifest(self.root,manifest)
        del cfg['helper_wake'];path.write_text(json.dumps(cfg));env['BB_DLSSG_CONFIG_SHA256']=hashlib.sha256(path.read_bytes()).hexdigest()
        del env['BB_DLSSG_HELPER_WAKE'];dlssg_backend.validate_manifest(self.root,manifest)

if __name__=='__main__':unittest.main()
