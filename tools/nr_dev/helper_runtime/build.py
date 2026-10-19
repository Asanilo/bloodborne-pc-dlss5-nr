#!/usr/bin/env python3
"""Compatibility entry point; the independent Helper repository owns the build."""
import runpy
from pathlib import Path

if __name__ == '__main__':
    runpy.run_path(str(Path(__file__).resolve().parents[1] / 'prepare_helper.py'), run_name='__main__')
