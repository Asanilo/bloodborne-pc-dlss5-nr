"""CPU protocol fixtures only. Mock completion fields are never GPU evidence."""
import json
from pathlib import Path
import tempfile
import threading
import time
import unittest
import stream_fixture as wire
import cpu_slot


class Process:
    def poll(self):
        return None


class StreamTests(unittest.TestCase):
    def test_mapped_payload_preserves_ack_and_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);files={}
            for role in ['color','depth','motion']:
                path=root/role;path.write_bytes(bytes([17])*cpu_slot.OFFSETS[role][1]);files[role]={'path':str(path)}
            with cpu_slot.mapping(root,create=True):pass
            wire.atomic(root/'stream-ready.json',{'session':'cpu','actual_GPU_complete':True})
            errors=[]
            def helper():
                try:
                    with cpu_slot.mapping(root) as payload:
                        for n in range(1,3):
                            request=wire.wait_record(root,'request.json',Process(),lambda r:r['sequence']==n,2)
                            self.assertEqual(request['transport'],'mapped-v1')
                            for role in files:self.assertEqual(payload[cpu_slot.OFFSETS[role][0]],17)
                            offset,size=cpu_slot.OFFSETS['output'];payload[offset:offset+size]=bytes([n])*size
                            request.pop('camera');request.update(actual_GPU_complete=True,native_gpu_readers=0,
                                result=1,disable_word=0,eligible=True,output_bytes=size)
                            wire.atomic(root/'response.json',request)
                            wire.wait_record(root,'consumed.json',Process(),lambda a:a['sequence']==n,2)
                except Exception as error:errors.append(error)
            worker=threading.Thread(target=helper);worker.start()
            wire.drive(root,{'frames':[{'source_game_frame':42+n,'reset':n==0,'files':files} for n in range(2)]},'cpu',Process(),'mapped-v1')
            worker.join(3);self.assertFalse(worker.is_alive());self.assertFalse(errors)
            self.assertEqual((root/'stream-1-interp-1920x1080.raw').read_bytes(),bytes([2])*cpu_slot.COLOR_BYTES)
            self.assertFalse((root/'input-color.raw').exists())

    def test_exact_sequence_ack_and_stop(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);source=root/'source';source.mkdir()
            files={}
            for role in ['color','depth','motion']:
                p=source/role;p.write_bytes(b'CPU fixture only');files[role]={'path':str(p)}
            record={'frames':[{'source_game_frame':42+n,'reset':n==0,'files':files} for n in range(3)]}
            wire.atomic(root/'stream-ready.json',{'session':'cpu','actual_GPU_complete':True})
            errors=[];acks=[]
            def helper():
                try:
                    for n in range(1,4):
                        r=wire.wait_record(root,'request.json',Process(),lambda r:r['sequence']==n,2)
                        r.pop('camera');r.update(actual_GPU_complete=True,native_gpu_readers=0,
                                                result=1,disable_word=1,eligible=False,output_bytes=0)
                        wire.atomic(root/'response.json',r)
                        a=wire.wait_record(root,'consumed.json',Process(),lambda a:a['sequence']==n,2)
                        acks.append(a)
                except Exception as error:
                    errors.append(error)
            worker=threading.Thread(target=helper);worker.start()
            responses=wire.drive(root,record,'cpu',Process());worker.join(3)
            self.assertFalse(worker.is_alive());self.assertFalse(errors)
            self.assertEqual([a['source_game_frame'] for a in acks],[42,43,44])
            self.assertEqual([r['previous_source_game_frame'] for r in responses],[0,42,43])
            self.assertEqual(json.loads((root/'stream-stop.json').read_text()),{'session':'cpu','sequence':3})

    def test_child_exit_does_not_invent_completion(self):
        class Exited:
            def poll(self):return 3
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(RuntimeError,'Helper exited'):
                wire.wait_record(Path(temp),'response.json',Exited(),lambda r:True,.1)

    def test_terminal_requires_actual_retirement_fields(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            wire.atomic(root/'stream-retired.json',{'actual_GPU_complete':True})
            with self.assertRaisesRegex(ValueError,'terminal'):
                wire.terminal(root,'cpu',[{'epoch':1,'source_game_frame':42}])


if __name__=='__main__':
    unittest.main()
