#!/usr/bin/env python3
"""Exercise clean donor placement in a private Good Egg fixture through the editor request seam."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    app = args.app.resolve()
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    (out / 'revision.json').write_text(json.dumps({
        'source': revision, 'app': str(app), 'sha256': hashlib.sha256(app.read_bytes()).hexdigest()
    }, indent=2) + '\n')
    server = [sys.executable, str(root / 'native/tools/test_server.py')]
    directory = out / 'server'
    env = dict(os.environ, PETARI_EDITOR='1', PETARI_MODS='LevelEditor', PETARI_EDITOR_PLACEMENT_TEST='1',
               PETARI_SURFACE_DUMP=str(out / 'surfaces'), PETARI_SURFACE_DUMP_LABELS='placement-clean,placement-launch',
               PETARI_SURFACE_DUMP_EVERY='5', PETARI_SURFACE_DUMP_SPAN='10')
    try:
        subprocess.run(server + ['start', '--app', str(app), '--dir', str(directory), '--owner', 'gravity',
                                 '--warm-current-stage'], env=env, check=True)
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            text = (directory / 'server.log').read_text(errors='replace')
            lines = [line for line in text.splitlines() if '[editor-placement]' in line]
            if any('FAIL:' in line for line in lines):
                print('\n'.join(lines))
                return 1
            if '[editor-placement] PASS' in text:
                print('\n'.join(lines))
                return 0
            time.sleep(.5)
        print('FAIL: timed out waiting for clean placement checks')
        return 1
    finally:
        if (directory / 'server.pid').exists():
            subprocess.run(server + ['stop', '--dir', str(directory)], check=False)
        shutil.rmtree(directory / 'user/cache', ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
