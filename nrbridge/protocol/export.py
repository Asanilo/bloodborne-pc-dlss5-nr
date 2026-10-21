#!/usr/bin/env python3
"""Export the versioned protocol without rewriting or relaxing its semantics."""
import argparse
import hashlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
SPEC_SHA = '671c7644c69de5f681b099fb4c3d402ba36f1e4538e65cd5538441e2bd47643b'
MODEL_SHA = '92c16609685a651a405842126b6160fd02376c2be034d2142f365bda119a0205'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def payloads():
    spec_bytes = (HERE / 'effective-spec.json').read_bytes()
    model_bytes = (HERE / 'native_runtime_model.h').read_bytes()
    if digest(spec_bytes) != SPEC_SHA or digest(model_bytes) != MODEL_SHA:
        raise ValueError('protocol canonical source drift; requires an explicit versioned change')
    spec = json.loads(spec_bytes)
    header = '#pragma once\nstatic const char pre_sr_spec_json[]=R"ASYNCSPEC(' + json.dumps(spec, indent=2) + ')ASYNCSPEC";\n'
    files = {'native_runtime_model.h': model_bytes,
             'pre_sr_wire.h': (HERE / 'pre_sr_wire.h').read_bytes(),
             'effective-spec.json': spec_bytes, 'pre_sr_spec.h': header.encode()}
    meta = {'schema': 'nrbridge-protocol-export-v1', 'wire_version': 1,
            'interface_revision': 4, 'runtime_model_schema': 'nr-runtime-model-v1',
            'job_model_schema': 'nr-runtime-job-model-v1',
            'exports': {name: digest(data) for name, data in files.items()}}
    files['protocol-export.json'] = (json.dumps(meta, indent=2) + '\n').encode()
    return files, meta


def export(output):
    output = Path(output)
    if not output.is_absolute():
        raise ValueError('absolute protocol output required')
    files, meta = payloads()
    for name, data in files.items():
        path = output / name
        if path.is_symlink() or (path.exists() and path.read_bytes() != data):
            raise ValueError('existing protocol export differs; preserved: ' + str(path))
    output.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        path = output / name
        if not path.exists():
            path.write_bytes(data)
    return meta


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(export(args.output), indent=2))
