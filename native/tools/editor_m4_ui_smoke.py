#!/usr/bin/env python3
"""Private M4 SDL UI checks: journal choices, and Observatory Home -> New Galaxy -> Edit."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

import stage_sweep as sweep
import warm_seed

ROOT = Path(__file__).resolve().parents[2]
MAIN = Path(subprocess.check_output(['git', 'rev-parse', '--git-common-dir'], cwd=ROOT, text=True).strip()).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--only', choices=['journal', 'home', 'all'], default='all')
    args = parser.parse_args()
    args.app = args.app.resolve()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ('journal-user', 'discard-user', 'home-user'):
        if (args.output / name).exists():
            parser.error('Use a fresh output folder; existing test users are not overwritten.')
    (args.output / 'revision.json').write_text(json.dumps({
        'source': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'app': str(args.app), 'sha256': hashlib.sha256(args.app.read_bytes()).hexdigest()
    }, indent=2) + '\n')
    wargs = argparse.Namespace(app=args.app, seed_root=str(MAIN / 'build/shader-seed'), stages=['EggStarGalaxy'],
                               owner='gravity', timeout=900, idle=900, force=False, user=None)
    wargs.sha = warm_seed.sha256_of(args.app)
    seed = warm_seed.ensure(wargs)
    users = []

    def run(name, user, extra, fixture=True):
        users.append(user)
        wargs.user = user
        warm_seed.prepare(wargs)
        a = argparse.Namespace(app=args.app, disc=MAIN / 'build/game-data/RMGE01', frames=30000,
                               idle_frames=20000, pipeline_policy='default')
        command, env_extra = sweep.app_command(a, user, 'EggStarGalaxy', 1)
        env = dict(os.environ, **env_extra)
        for key in ('PETARI_EDITOR_JOURNAL_TEST', 'PETARI_EDITOR_M4_UI_TEST', 'PETARI_EDITOR_NEWGALAXY_TEST',
                    'PETARI_OPEN_STAGE', 'PETARI_OPEN_EDIT', 'PETARI_STAGE_FOLLOW'):
            env.pop(key, None)
        if not fixture:
            command = command[:-2]
            env.pop('PETARI_STAGE', None)
            env.pop('PETARI_SCENARIO', None)
            env.update(PETARI_SMOKE='observe', PETARI_OPEN_FILE='1')
        env.update(PETARI_LOCK_CLASS='functional', PETARI_FAST_BOOT='1', PETARI_STAGE_GATE_MS='0',
                   PETARI_SHADER_SEED_DIR=str(seed), PETARI_EDITOR='1', PETARI_MODS='LevelEditor',
                   PETARI_SURFACE_DUMP=str(args.output / name / 'surfaces'),
                   PETARI_SURFACE_DUMP_LABELS='m4-home,m4-new-sheet,m4-created,m4-journal,m4-journal-result,m4-settings-live,m4-settings-added,m4-settings-final',
                   PETARI_SURFACE_DUMP_EVERY='5', PETARI_SURFACE_DUMP_SPAN='10')
        env.update(extra)
        log = args.output / (name + '.log')
        with log.open('wb') as stream:
            code = subprocess.run([str(MAIN / 'build/locked-app.sh'), 'gravity', '/usr/bin/perl', '-e',
                                   'alarm shift; exec @ARGV or die', '900'] + command,
                                  cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT).returncode
        text = log.read_text(errors='replace')
        for line in text.splitlines():
            if '[editor-m4-ui]' in line or '[editor-journal]' in line:
                print(name + ': ' + line, flush=True)
        if extra.get('PETARI_EDITOR_JOURNAL_TEST') == 'kill':
            return code != 0 and any('[editor-journal]' in line and 'killing' in line for line in text.splitlines())
        return '[editor-m4-ui] PASS' in text and '[editor-m4-ui] FAIL' not in text

    try:
        if args.only in ('journal', 'all'):
            user = args.output / 'journal-user'
            sweep.load_fixture_module().create(MAIN / 'build/observatory-user-2', user, 'stage')
            if not run('journal-prepare', user, {'PETARI_EDITOR_JOURNAL_TEST': 'kill'}):
                return 1
            journal = Path('editor/journal/EggStarGalaxy.json')
            if not (user / journal).is_file():
                print('FAIL: no crash journal')
                return 1
            discard = args.output / 'discard-user'
            shutil.copytree(user, discard, ignore=shutil.ignore_patterns('cache'))
            if not run('journal-restore', user, {'PETARI_EDITOR_M4_UI_TEST': 'journal-restore'}):
                return 1
            if not run('journal-discard', discard, {'PETARI_EDITOR_M4_UI_TEST': 'journal-discard'}):
                return 1
            if (discard / journal).exists():
                print('FAIL: Discard did not remove journal')
                return 1
        if args.only in ('home', 'all'):
            user = args.output / 'home-user'
            shutil.copytree(MAIN / 'build/saves/all-missions/NAND', user / 'NAND')
            (user / 'mods.txt').write_text('LevelEditor=on\n')
            if not run('home', user, {'PETARI_EDITOR_M4_UI_TEST': 'home'}, fixture=False):
                return 1
            mods = list((user / 'mods').glob('*/galaxy.json'))
            if not mods:
                print('FAIL: Create did not write a mod galaxy')
                return 1
        print('PASS: M4 UI checks')
        return 0
    finally:
        for user in users:
            shutil.rmtree(user / 'cache', ignore_errors=True)


if __name__ == '__main__':
    sys.exit(main())
