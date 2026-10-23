"""Finite CPU producer/consumer for the same protocol used by the Native FG bridge."""
import json
from pathlib import Path
import shutil
import time
from contextlib import nullcontext
import cpu_slot


def atomic(path, record):
    temp = path.with_suffix('.tmp')
    temp.write_text(json.dumps(record)+'\n')
    temp.replace(path)


def wait_record(run, name, process, predicate, timeout=30):
    deadline = time.monotonic()+timeout
    while time.monotonic()<deadline:
        path = run/name
        if path.is_file():
            record = json.loads(path.read_text())
            if predicate(record):
                return record
        if process.poll() is not None:
            raise RuntimeError(f'Helper exited before {name}; actual state in probe.log')
        time.sleep(.001)
    raise RuntimeError(f'CPU control timeout waiting for {name}; no forced GPU retirement')


def drive(run, record, session, process, transport="files-v1"):
    with cpu_slot.mapping(run) if transport=="mapped-v1" else nullcontext(None) as payload:
        return _drive(run,record,session,process,transport,payload)


def _drive(run, record, session, process, transport, payload):
    wait_record(run,'stream-ready.json',process,
                lambda r:r.get('session')==session and r.get('actual_GPU_complete') is True)
    previous, epoch, responses = 0, 0, []
    for n, frame in enumerate(record['frames']):
        sequence=n+1
        reset=frame['reset'] or n==0
        if reset:
            epoch+=1
        for role in ['color','depth','motion']:
            if payload is None:
                shutil.copyfile(frame['files'][role]['path'],run/f'input-{role}.raw')
            else:
                offset,size=cpu_slot.OFFSETS[role];data=Path(frame['files'][role]['path']).read_bytes()
                if len(data)!=size:raise ValueError("exact mapped input length required")
                payload[offset:offset+size]=data
        request={'schema':'dlssg-cpu-stream-v1','session':session,'sequence':sequence,'epoch':epoch,
                 'source_game_frame':frame['source_game_frame'],'previous_source_game_frame':previous,
                 'reset':reset,'display':0,'transport':transport,'wire_order':'game-scene-bytes','camera':frame}
        atomic(run/'request.json',request)
        response=wait_record(run,'response.json',process,lambda r:r.get('sequence')==sequence)
        for key,value in request.items():
            if key!='camera' and response.get(key)!=value:
                raise ValueError('stream exact response pair mismatch: '+key)
        if response.get('actual_GPU_complete') is not True or response.get('native_gpu_readers')!=0:
            raise ValueError('actual private GPU completion required')
        if response['eligible']:
            if response['result']!=1 or response['disable_word']&255 or response['output_bytes']!=1920*1080*4:
                raise ValueError('GPU disable/output contract mismatch')
            output=run/'output-color.raw'
            if payload is None and output.stat().st_size!=1920*1080*4:
                raise ValueError('bounded output length required')
            # Preserve just the same first three eligible images for the format/transport comparison.
            if n<4:
                if payload is None:shutil.copyfile(output,run/f'stream-{n}-interp-1920x1080.raw')
                else:
                    offset,size=cpu_slot.OFFSETS['output']
                    (run/f'stream-{n}-interp-1920x1080.raw').write_bytes(payload[offset:offset+size])
        atomic(run/'consumed.json',{'session':session,'sequence':sequence,'epoch':epoch,
                                   'source_game_frame':frame['source_game_frame'],
                                   'disposition':'offline inspected CPU output; no Native GPU reader'})
        responses.append(response)
        previous=frame['source_game_frame']
    atomic(run/'stream-stop.json',{'session':session,'sequence':len(responses)})
    return responses


def terminal(run, session, responses):
    record=json.loads((run/'stream-retired.json').read_text())
    expected={'schema':'dlssg-cpu-stream-v1','session':session,'sequence':len(responses),
              'epoch':responses[-1]['epoch'],'source_game_frame':responses[-1]['source_game_frame'],
              'cpu_slot_consumed':True,'actual_GPU_complete':True,'feature_released':True,
              'device_destroyed':True,'native_gpu_readers':0}
    if record!=expected:
        raise ValueError('stream terminal actual resource/CPU consumption mismatch')
    return record
