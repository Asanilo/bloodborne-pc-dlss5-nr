#!/usr/bin/env python3
"""Compatibility import/CLI for Native's peer retirement auditor."""
import importlib.util as _util
from pathlib import Path as _Path

_spec = _util.spec_from_file_location('nrbridge_peer_retirement_compat', _Path(__file__).with_name('peer_retirement.py'))
_peer = _util.module_from_spec(_spec)
_spec.loader.exec_module(_peer)
globals().update({name: value for name, value in vars(_peer).items() if not name.startswith('_')})

if __name__ == '__main__':
    raise SystemExit(main())
