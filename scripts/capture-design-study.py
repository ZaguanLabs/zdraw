#!/usr/bin/env python3
"""Capture the design study in real xterm windows on a private Xvfb display.

Optional development tools: Xvfb, xterm, xwininfo, ImageMagick import.
Never connects to the user's desktop. Output is screenshots plus provenance.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import select
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def capture(output, example='design-study', terminal='xterm-256color'):
    for program in ('Xvfb', 'xterm', 'xwininfo', 'import'):
        if not shutil.which(program):
            raise RuntimeError(f'{program} is required for optional graphical captures')
    output.mkdir(parents=True, exist_ok=True)
    records = dict(format=f'zdraw-{example}-captures-1', platform=platform.platform(),
                   captured_at=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                   source_sha256=hashlib.sha256((ROOT / f'examples/{example}.zsh').read_bytes()).hexdigest(),
                   module_sha256=hashlib.sha256((ROOT / '.build/modules/zdraw.so').read_bytes()).hexdigest(),
                   xterm=subprocess.check_output(['xterm', '-version'], text=True).strip(),
                   shell=subprocess.check_output([str(ROOT / '.build/zsh/Src/zsh'), '--version'], text=True).strip(),
                   font='DejaVu Sans Mono 11', locale='C.UTF-8', term=terminal, scenarios=[])
    if example in ('linked-detail', 'change-gutter', 'status-strip'):
        records['component_sha256'] = hashlib.sha256((ROOT / f'examples/components/{example}.zsh').read_bytes()).hexdigest()
    if example == 'review-composition':
        records['components_sha256'] = {
            name: hashlib.sha256((ROOT / f'examples/components/{name}.zsh').read_bytes()).hexdigest()
            for name in ('linked-detail', 'change-gutter', 'status-strip')}
    if example == 'color-studio':
        records['helpers_sha256'] = {
            name: hashlib.sha256((ROOT / f'lib/ui/{name}.zsh').read_bytes()).hexdigest()
            for name in ('core', 'color')}
    rr, rw = os.pipe()
    server = subprocess.Popen(['Xvfb', '-displayfd', str(rw), '-screen', '0', '1600x1000x24', '-nolisten', 'tcp'],
                              pass_fds=(rw,), stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    os.close(rw)
    try:
        if not select.select([rr], [], [], 10)[0]:
            raise RuntimeError('Xvfb startup timeout')
        display = ':' + os.read(rr, 64).decode().strip()
        env = os.environ.copy()
        for key in ('TERM', 'TERMINFO', 'TERMINFO_DIRS', 'LINES', 'COLUMNS', 'TMUX', 'STY', 'NO_COLOR', 'XENVIRONMENT'):
            env.pop(key, None)
        env.update(DISPLAY=display, LC_ALL='C.UTF-8')
        scenarios = []
        for design in ('quiet', 'workbench', 'expressive'):
            scenarios.extend([
                (design, design, 'ready', '120x32', 'list', 'auto'),
                (design + '-narrow', design, 'ready', '44x20', 'detail', 'auto'),
            ])
        scenarios.extend([
            ('quiet-empty', 'quiet', 'empty', '100x28', 'list', 'auto'),
            ('workbench-busy', 'workbench', 'busy', '120x32', 'list', 'auto'),
            ('expressive-error', 'expressive', 'error', '100x28', 'detail', 'auto'),
            ('expressive-mono', 'expressive', 'ready', '100x28', 'list', 'mono'),
            ('workbench-16', 'workbench', 'ready', '100x28', 'list', '16'),
        ])
        if example == 'linked-detail':
            scenarios = [
                ('dark', 'dark', 'ready', '120x30', 'list', 'auto'),
                ('compact', 'dark', 'ready', '120x30', 'list', 'auto'),
                ('light', 'light', 'ready', '100x26', 'list', 'auto'),
                ('catalog', 'light', 'ready', '100x26', 'list', 'auto'),
                ('narrow-list', 'dark', 'ready', '38x16', 'list', 'auto'),
                ('narrow-detail', 'dark', 'ready', '38x16', 'detail', 'auto'),
                ('mono', 'dark', 'ready', '100x26', 'list', 'mono'),
                ('empty', 'dark', 'empty', '100x26', 'list', 'auto'),
            ]
        if example == 'change-gutter':
            scenarios = [
                ('dark', 'dark', 'ready', '100x30', 'list', 'auto'),
                ('light', 'light', 'ready', '100x30', 'list', 'auto'),
                ('narrow', 'dark', 'ready', '38x22', 'list', 'auto'),
                ('mono', 'dark', 'ready', '80x28', 'list', 'mono'),
                ('variant', 'dark', 'ready', '100x30', 'list', 'auto'),
                ('empty', 'dark', 'empty', '80x24', 'list', 'auto'),
            ]
        if example == 'status-strip':
            scenarios = [
                ('dark', 'dark', 'working', '100x24', 'list', 'auto'),
                ('light', 'light', 'working', '100x24', 'list', 'auto'),
                ('narrow', 'dark', 'working', '38x22', 'list', 'auto'),
                ('compact', 'dark', 'working', '80x18', 'list', 'auto'),
                ('unknown', 'dark', 'working', '100x24', 'list', 'auto'),
                ('failed', 'dark', 'failed', '100x24', 'list', 'auto'),
                ('mono', 'dark', 'working', '80x24', 'list', 'mono'),
                ('variant', 'dark', 'working', '100x24', 'list', 'auto'),
            ]
        if example == 'review-composition':
            scenarios = [
                ('keys', 'dark', 'ready', '80x24', 'list', 'auto'),
                ('keys-narrow', 'dark', 'ready', '26x11', 'list', 'auto'),
                ('dark', 'dark', 'ready', '120x28', 'list', 'auto'),
                ('compact', 'dark', 'ready', '120x28', 'list', 'auto'),
                ('short', 'dark', 'ready', '38x11', 'detail', 'auto'),
                ('light', 'light', 'ready', '100x24', 'list', 'auto'),
                ('narrow-list', 'dark', 'ready', '38x20', 'list', 'auto'),
                ('narrow-detail', 'dark', 'ready', '38x20', 'detail', 'auto'),
                ('mono', 'dark', 'ready', '100x24', 'list', 'mono'),
                ('variant', 'dark', 'ready', '100x24', 'list', 'auto'),
                ('empty', 'dark', 'empty', '100x24', 'list', 'auto'),
            ]
        if example == 'color-studio':
            scenarios = [
                ('dark', 'dark', 'ready', '100x24', 'list', 'auto'),
                ('light', 'light', 'ready', '100x24', 'list', 'auto'),
                ('narrow', 'dark', 'ready', '28x10', 'list', 'auto'),
                ('mono', 'dark', 'ready', '80x24', 'list', 'mono'),
            ]
        with tempfile.TemporaryDirectory(prefix='study-capture-', dir=ROOT / '.build') as temporary:
            for name, design, state, geometry, focus, profile in scenarios:
                directory = Path(temporary) / name
                directory.mkdir()
                os.mkfifo(directory / 'continue')
                command = ['xterm', '-display', display, '-title', 'zdraw-study', '-tn', terminal,
                           '-geometry', geometry + '+0+0', '+sb', '-fa', 'DejaVu Sans Mono', '-fs', '11',
                           '-xrm', 'XTerm*cursorBlink: false', '-xrm', 'XTerm*cursorUnderLine: true',
                           '-e', str(ROOT / '.build/zsh/Src/zsh'), '-df',
                           str(ROOT / 'scripts/design-study-frame.zsh'), str(directory), focus]
                if example == 'color-studio':
                    command += ['--example', example, '--theme', design, '--profile', profile]
                elif example in ('linked-detail', 'change-gutter', 'status-strip', 'review-composition'):
                    command += ['--example', example, '--theme', design, '--profile', profile]
                    if example == 'review-composition' and name in ('keys', 'keys-narrow'):
                        command += ['--keys']
                    if name == 'compact':
                        command += ['--compact'] if example in ('status-strip', 'review-composition') else ['--item-gap', '0']
                    if example == 'status-strip':
                        command += ['--phase', state]
                        if name == 'unknown':
                            command += ['--unknown']
                    if name == 'catalog':
                        command += ['--dataset', 'catalog', '--variant']
                    if name == 'variant':
                        command += ['--variant']
                    if state == 'empty':
                        command += ['--empty']
                else:
                    command += ['--design', design, '--state', state, '--profile', profile]
                if profile in ('mono', '16') and example not in ('status-strip', 'color-studio'):
                    command.append('--ascii')
                process = subprocess.Popen(command, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                try:
                    deadline = time.monotonic() + 15
                    while not (directory / 'ready').exists():
                        if process.poll() is not None:
                            raise RuntimeError(process.stderr.read().decode())
                        if time.monotonic() > deadline:
                            raise RuntimeError(f'{name}: first frame timed out')
                        time.sleep(.02)
                    time.sleep(.2)  # Presentation completed; allow emulator paint to settle.
                    tree = subprocess.check_output(['xwininfo', '-display', display, '-root', '-tree'], text=True)
                    match = re.search(r'(0x[0-9a-f]+) "zdraw-study"', tree)
                    if not match:
                        raise RuntimeError('private xterm window not found')
                    subprocess.run(['import', '-display', display, '-window', match[1], str(output / (name + '.png'))],
                                   check=True, timeout=10)
                    with (directory / 'continue').open('w') as control:
                        control.write('continue\n')
                    process.wait(timeout=5)
                    if process.returncode:
                        raise RuntimeError(f'{name}: xterm exited {process.returncode}')
                    records['scenarios'].append(dict(name=name, design=design, state=state,
                                                     geometry=geometry, focus=focus, profile=profile,
                                                     options=command[command.index(focus) + 1:]))
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            process.kill(); process.wait()
                    process.stderr.close()
    finally:
        os.close(rr)
        server.terminate()
        try:
            server.wait(timeout=3)
        except subprocess.TimeoutExpired:
            server.kill(); server.wait()
        server.stderr.close()
    (output / 'captures.json').write_text(json.dumps(records, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--term', choices=('xterm-256color', 'xterm-direct'), default='xterm-256color')
    parser.add_argument('--example', choices=('design-study', 'linked-detail', 'change-gutter', 'status-strip', 'review-composition', 'color-studio'), default='design-study')
    args = parser.parse_args()
    capture(args.output.resolve(), args.example, args.term)
