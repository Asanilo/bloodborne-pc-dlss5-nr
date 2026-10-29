#!/usr/bin/env python3
"""Print one acceptance round's result, pacing, proofs and display summary."""
import json
import sys
from pathlib import Path


def main() -> None:
    run, kind = Path(sys.argv[1]), sys.argv[2]
    result = json.loads((run / 'result.json').read_text()) if (run / 'result.json').exists() else {}
    print('ROUND', kind, 'status', result.get('status'), 'returncode', result.get('returncode'))
    manifest = json.loads((run / 'manifest.json').read_text()) if (run / 'manifest.json').exists() else {}
    env = manifest.get('environment', {})
    print('  pacing fps_limit', env.get('BB_FPS_LIMIT'), 'present_mode', env.get('BB_PRESENT_MODE'))
    log = run / 'game.log'
    if log.exists():
        text = log.read_text(errors='replace')
        print('  markers', [line.split(' ', 1)[0] for line in text.splitlines()
                            if line.startswith(('SM_EXIT_', 'FG_EXIT_',
                                                'NATIVE_ASYNC_PRE_SR_EXIT_PROOF'))])
        print('  device_lost', 'DeviceLost' in text or 'DEVICE_LOST' in text)
    summary = run / 'display-frames.summary'
    if summary.exists():
        print('  display', summary.read_text().strip())


if __name__ == '__main__':
    main()
