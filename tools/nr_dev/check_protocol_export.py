#!/usr/bin/env python3
"""Verify Native's local copy of the versioned Helper protocol before building."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

DIRECTORY = Path('gpu/render')
HEADER = 'native_runtime_model.h'
PROVENANCE = 'native_runtime_model.export.json'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def exact(value, keys):
    if not isinstance(value, dict) or set(value) != set(keys):
        raise ValueError('protocol provenance exact object/schema required')


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('protocol provenance duplicate key: ' + key)
        result[key] = value
    return result


def checked(native_root):
    directory = Path(native_root) / DIRECTORY
    header, provenance = directory / HEADER, directory / PROVENANCE
    for path in (header, provenance):
        if path.is_symlink() or not path.is_file():
            raise ValueError('missing regular local protocol input: ' + str(path))
    raw = provenance.read_bytes()
    meta = json.loads(raw, object_pairs_hook=unique)
    exact(meta, ('schema', 'source', 'consumed', 'export_manifest', 'export_manifest_sha256', 'update'))
    if meta['schema'] != 'nrbridge-native-protocol-copy-v1' or meta['consumed'] not in ([HEADER], [HEADER, 'nr_dimensions.h']):
        raise ValueError('Native protocol copy schema/consumed mismatch')
    source = meta['source']
    exact(source, ('repository', 'commit', 'header', 'exporter'))
    if (source['repository'] != 'Asanilo/OptiScaler-Linux-NR'
            or not isinstance(source['commit'], str) or not re.fullmatch('[0-9a-f]{40}', source['commit'])
            or source['header'] != 'nrbridge/protocol/' + HEADER
            or source['exporter'] != 'nrbridge/protocol/export.py'):
        raise ValueError('Helper protocol source identity mismatch')
    export = meta['export_manifest']
    exact(export, ('schema', 'wire_version', 'interface_revision', 'runtime_model_schema', 'job_model_schema', 'exports'))
    if (export['schema'] != 'nrbridge-protocol-export-v1'
            or type(export['wire_version']) is not int or export['wire_version'] != 1
            or type(export['interface_revision']) is not int or export['interface_revision'] not in (3,4)
            or export['runtime_model_schema'] != 'nr-runtime-model-v1'
            or export['job_model_schema'] != 'nr-runtime-job-model-v1'):
        raise ValueError('unsupported Helper protocol version/schema')
    exact(export['exports'], (HEADER, 'pre_sr_wire.h', 'pre_sr_spec.h', 'effective-spec.json', *(['nr_dimensions.h'] if 'nr_dimensions.h' in meta['consumed'] else [])))
    if 'nr_dimensions.h' in meta['consumed'] and sha((directory / 'nr_dimensions.h').read_bytes()) != export['exports']['nr_dimensions.h']:
        raise ValueError('dimensions protocol export drift')
    for digest in export['exports'].values():
        if not isinstance(digest, str) or not re.fullmatch('[0-9a-f]{64}', digest):
            raise ValueError('invalid Helper protocol export SHA')
    # Reproduce the exporter's exact JSON serialization. Native only consumes
    # HEADER; the other entries remain metadata, without importing Helper code.
    exported_bytes = (json.dumps(export, indent=2) + '\n').encode()
    if sha(exported_bytes) != meta['export_manifest_sha256']:
        raise ValueError('Helper protocol export manifest SHA mismatch')
    actual = sha(header.read_bytes())
    if actual != export['exports'][HEADER]:
        raise ValueError('Native runtime protocol header SHA mismatch')
    return {'schema': meta['schema'], 'source': source,
            'wire_version': export['wire_version'], 'interface_revision': export['interface_revision'],
            'runtime_model_schema': export['runtime_model_schema'], 'job_model_schema': export['job_model_schema'],
            'header_sha256': actual, 'provenance_sha256': sha(raw),
            'export_manifest_sha256': meta['export_manifest_sha256']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-root', type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    try:
        print(json.dumps(checked(args.native_root), sort_keys=True))
        return 0
    except (OSError, ValueError, TypeError, KeyError) as error:
        print('nr-dev protocol: ' + str(error), file=sys.stderr)
        return 3


if __name__ == '__main__':
    raise SystemExit(main())
