#!/usr/bin/env python3
"""Check live array previews, ground-snapped copies and compound undo/redo in a private stage fixture."""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys

from test_server import main_checkout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--stage', default='EggStarGalaxy')
    parser.add_argument('--owner', default='editor-paths')
    parser.add_argument('--idle-frames', type=int, default=1200)
    parser.add_argument('--source-class', help='Require a visible model of this class for the preview capture')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    main = main_checkout()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    user = output / 'user'
    app = args.app.resolve()
    subprocess.run([sys.executable, str(root / 'native/tools/create_observatory_fixture.py'),
                    '--source', str(main / 'build/observatory-user-2'), '--output', str(user), '--kind', 'stage'], check=True)
    (output / 'revision.txt').write_text(subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True)
                                       + hashlib.sha256(app.read_bytes()).hexdigest() + '\n')
    env = dict(os.environ, PETARI_SMOKE='stage', PETARI_STAGE=args.stage, PETARI_SCENARIO='1',
               PETARI_SMOKE_FRAMES='12000', PETARI_STAGE_IDLE_FRAMES=str(max(400, args.idle_frames)), PETARI_STAGE_GATE_MS='0',
               PETARI_FAST_BOOT='1', PETARI_PIPELINE_GLOBAL_PRECOMPILE='0', PETARI_LOCK_CLASS='functional',
               PETARI_MODS='LevelEditor', PETARI_EDITOR_ARRAY_TEST=str(output),
               PETARI_SURFACE_DUMP=str(output / 'surfaces'),
               PETARI_SURFACE_DUMP_LABELS='array-line,array-axis,array-ring,array-applied',
               PETARI_SURFACE_DUMP_EVERY='5', PETARI_SURFACE_DUMP_SPAN='15')
    if args.source_class:
        env['PETARI_EDITOR_ARRAY_CLASS'] = args.source_class
    command = [str(main / 'build/locked-app.sh'), args.owner, '/usr/bin/perl', '-e',
               'alarm shift; exec @ARGV or die', '600', str(app), '--disc', str(main / 'build/game-data/RMGE01'),
               '--user', str(user), '--test-fixture', 'stage']
    try:
        with (output / 'run.log').open('w') as log:
            result = subprocess.run(command, env=env, cwd=root, stdout=log, stderr=subprocess.STDOUT)
        text = (output / 'run.log').read_text(errors='replace')
        for line in text.splitlines():
            if '[editor-array]' in line or 'PETARI SMOKE RESULT:' in line:
                print(line)
        if result.returncode or '[editor-array] PASS' not in text or '[editor-array] FAIL' in text:
            print('FAIL: see', output / 'run.log')
            return 1
        return 0
    finally:
        shutil.rmtree(user / 'cache', ignore_errors=True)


if __name__ == '__main__':
    raise SystemExit(main())
