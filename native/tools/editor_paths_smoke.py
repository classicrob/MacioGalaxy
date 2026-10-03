#!/usr/bin/env python3
"""Exercise live child-zone rail editing and undo in one background test-server launch."""
import argparse
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--owner', default='editor-paths')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    server = output / 'server'
    command = [sys.executable, str(root / 'native/tools/test_server.py')]
    env = dict(os.environ, PETARI_EDITOR='1', PETARI_MODS='LevelEditor',
               PETARI_EDITOR_PATHS_TEST=str(output), PETARI_SURFACE_DUMP=str(output),
               PETARI_SURFACE_DUMP_LABELS='paths-edited,paths-wires', PETARI_SURFACE_DUMP_EVERY='5',
               PETARI_SURFACE_DUMP_SPAN='15')
    try:
        # Warm without the test hook: other seed stages need not contain this fixture's riders.
        subprocess.run([sys.executable, str(root / 'native/tools/warm_seed.py'), 'env',
                        '--app', str(args.app.resolve()), '--owner', args.owner],
                       check=True, stdout=subprocess.DEVNULL, cwd=root)
        result = subprocess.run(command + ['start', '--app', str(args.app.resolve()), '--dir', str(server),
                                          '--owner', args.owner, '--boot-timeout', '300'], env=env, cwd=root)
        log = (server / 'server.log').read_text(errors='replace') if (server / 'server.log').exists() else ''
        for line in log.splitlines():
            if '[editor-paths]' in line:
                print(line)
        if result.returncode or '[editor-paths] PASS' not in log or '[editor-paths] FAIL' in log:
            print('FAIL: see', server / 'server.log')
            return 1
        print('PASS: edited rail reached re-created rider, undo restored it, no scene reload')
        return 0
    finally:
        if (server / 'server.pid').exists():
            subprocess.run(command + ['stop', '--dir', str(server)], cwd=root)


if __name__ == '__main__':
    raise SystemExit(main())
