#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Freeze Python source/asset checks, not a PE/compiler/model runtime result."""
import argparse
import ast
from datetime import datetime,timezone
import json
import os
from pathlib import Path
import subprocess
import sys
from common import ROOT,sha,strict_json


def build(assets,out):
    if out.exists():raise ValueError('refuse existing build evidence path')
    out.mkdir(parents=True);(out/'source').mkdir()
    names=['fixture-spec.json','CASE-SCHEMA.md','common.py','generate.py','audit.py','checks.py','build_only.py','README.md']
    pre={str(ROOT/n):sha(ROOT/n) for n in names}
    for n in names:
        p=ROOT/n;(out/'source'/n).write_bytes(p.read_bytes())
        if n.endswith('.py'):compile(ast.parse(p.read_text(),filename=str(p)),str(p),'exec')
    cmd=[sys.executable,str(ROOT/'checks.py'),'--assets',str(assets)]
    env=os.environ.copy();env['PYTHONDONTWRITEBYTECODE']='1'
    try:
        r=subprocess.run(cmd,env=env,cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
        code=r.returncode;output=r.stdout
    except subprocess.TimeoutExpired as e:
        code=124;output=(e.stdout or b'')+b'\nCPU_CHECK_WATCHDOG120s\n'
    (out/'cpu-regression.log').write_bytes(output)
    check=json.loads(output) if code==0 else None
    if code==0:(out/'cpu-regression.json').write_text(json.dumps(check,indent=2)+'\n')
    post={str(ROOT/n):sha(ROOT/n) for n in names}
    if post!=pre:raise ValueError('source changed during offline check')
    index=strict_json(assets/'asset-index.json')
    sources=index['assets']+index['cases']
    for obj in sources:
        if sha(obj['path'])!=obj['sha256']:raise ValueError('asset/case changed during check')
    result={'status':'PASS' if code==0 else 'FAIL','scope':'PYTHON_AST_AND_CPU_ORACLE_REGRESSION_ONLY',
            'created_utc':datetime.now(timezone.utc).isoformat(),'exit_code':code,'command':cmd,
            'sources':pre,'sources_pre':pre,'sources_post':post,
            'asset_index':{'path':str(assets/'asset-index.json'),'sha256':sha(assets/'asset-index.json')},
            'assets':sources,'cpu_regression':check,'GPU':'NOT_RUN','Wine':'NOT_RUN','PE_build':'A_OWNED_SEPARATE','model_calls':0}
    (out/'build-result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'status':result['status'],'scope':result['scope'],'evidence':str(out/'build-result.json'),'exit_code':code}))
    return code


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--assets',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();raise SystemExit(build(a.assets.resolve(),a.out.resolve()))
