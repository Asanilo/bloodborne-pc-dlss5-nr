#!/usr/bin/env python3
"""Optional private-image two-near-allocation replay; skips when the private image is absent."""
from pathlib import Path
import argparse, os, subprocess, tempfile
parser=argparse.ArgumentParser()
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--image', type=Path,
                    help='absolute private camera image; omitted means not requested')
args=parser.parse_args()
if args.image is not None:
    if not args.image.is_absolute():
        parser.error('--image must be an absolute path')
    if not args.image.is_file():
        parser.error(f'--image does not exist: {args.image}')
root=Path(__file__).resolve().parents[1]
if args.image is None:
    print('SKIP: private camera image not requested (no --image / BB_NATIVE_MOUSE_IMAGE)')
    raise SystemExit(0)
image=args.image
print(f'private image sha256={__import__("hashlib").sha256(image.read_bytes()).hexdigest()}')
with tempfile.TemporaryDirectory(prefix='bb-fps-real-') as tmp:
    exe=Path(tmp)/'replay'
    san=['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if args.sanitize else []
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',
             UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1') if args.sanitize else dict(os.environ)
    subprocess.run([os.environ.get('CXX','clang++'),'-std=c++23','-O1','-g','-UNDEBUG','-pthread',
                    '-mavx','-msse4.1','-mxsave',*san,
                    *( ['-DBB_ASAN_REPLAY'] if args.sanitize else [] ),
                    '-I'+str(root),'-I'+str(root/'gpu'),
                    str(root/'tests/test_native_mouse_fps_real.cpp'),
                    str(root/'gpu/shim/bbport_frame_rate.cpp'),
                    str(root/'gpu/shim/bbport_mouse_look.cpp'),'-o',str(exe)],check=True,env=env)
    for mode in ('free','occupied','badproof'):
        r=subprocess.run([str(exe),mode,str(image)],check=True,text=True,env=env,
                         stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        print(r.stdout,end='')
