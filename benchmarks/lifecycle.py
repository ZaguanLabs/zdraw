#!/usr/bin/env python3
"""Seeded lifecycle stress in a private PTY, optionally with full Memcheck."""
import argparse
import errno
import fcntl
import json
import os
from pathlib import Path
import pty
import re
import select
import shutil
import signal
import struct
import termios
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def run(cycles, seed, mode, shell, modules, guarded, valgrind=None, timeout=300):
    ready_r, ready_w = os.pipe()
    pid, terminal = pty.fork()
    if pid == 0:
        os.close(ready_w)
        os.read(ready_r, 1)
        os.close(ready_r)
        os.environ.update(TERM='xterm-256color',
                          LC_ALL=os.environ.get('ZDRAW_TEST_LOCALE', 'C.UTF-8'),
                          ZDRAW_STRESS_CYCLES=str(cycles), ZDRAW_STRESS_SEED=str(seed))
        os.environ['ZDRAW_STRESS_GUARDED'] = str(int(guarded))
        for key in ('LINES', 'COLUMNS'):
            os.environ.pop(key, None)
        prefix = [] if valgrind is None else [
            'valgrind', '--tool=memcheck', '--leak-check=full',
            '--show-leak-kinds=all', '--errors-for-leak-kinds=definite,indirect',
            '--error-exitcode=99', f'--log-file={valgrind}']
        command = [*prefix, str(shell), '-df',
                   str(ROOT / 'tests/lifecycle-stress.zsh'),
                   str(modules), mode]
        os.execvp(command[0], command)
    os.close(ready_r)
    reaped = False
    output = bytearray()
    started = time.monotonic()
    try:
        fcntl.ioctl(terminal, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
        original = termios.tcgetattr(terminal)
        os.write(ready_w, b'!')
        while True:
            if time.monotonic() - started >= timeout:
                raise TimeoutError(f'lifecycle stress exceeded {timeout}s: {output[-2000:]!r}')
            if not select.select([terminal], [], [], .1)[0]:
                continue
            try:
                chunk = os.read(terminal, 65536)
            except OSError as error:
                if error.errno != errno.EIO:
                    raise
                chunk = b''
            if not chunk:
                break
            output.extend(chunk)
        _, status = os.waitpid(pid, 0)
        reaped = True
        code = os.waitstatus_to_exitcode(status)
        if code or b'LIFECYCLE STRESS PASS' not in output:
            raise RuntimeError(f'exit {code}: {output[-800:]!r}; memcheck log: {valgrind}')
        if termios.tcgetattr(terminal) != original:
            raise RuntimeError('terminal modes were not restored')
        memory = None
        if valgrind:
            log = valgrind.read_text()
            memory = {kind.replace(' ', '_') + '_bytes': int(re.search(
                rf'{kind}:\s+([\d,]+) bytes', log).group(1).replace(',', ''))
                for kind in ('definitely lost', 'indirectly lost', 'possibly lost', 'still reachable')}
            memory['errors'] = int(re.search(r'ERROR SUMMARY: ([\d,]+) errors', log).group(1).replace(',', ''))
        return dict(cycles=cycles, seed=seed, mode=mode,
                    elapsed_seconds=time.monotonic()-started,
                    terminal_restored=True, output_bytes=len(output),
                    memcheck=memory,
                    memcheck_log=str(valgrind) if valgrind else None)
    finally:
        if not reaped:
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            os.waitpid(pid, 0)
        os.close(terminal)
        os.close(ready_w)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cycles', type=int, default=1000)
    parser.add_argument('--seed', type=int, default=20261113)
    parser.add_argument('--mode', choices=('standalone', 'stock'), default='standalone')
    parser.add_argument('--valgrind-log', type=Path)
    parser.add_argument('--timeout', type=int, default=300)
    args = parser.parse_args()
    if not 1 <= args.cycles <= 10000 or not 0 <= args.seed <= 999999999 or args.timeout < 1:
        parser.error('cycles must be 1..10000, seed 0..999999999 and timeout positive')
    if args.valgrind_log:
        args.valgrind_log.parent.mkdir(parents=True, exist_ok=True)
    # A later build may replace the staged module. Never execute from an inode
    # which make build can truncate while this longer investigation is running.
    with tempfile.TemporaryDirectory(prefix='lifecycle-', dir=ROOT / '.build') as tmp:
        tmp = Path(tmp)
        shell = tmp / 'zsh'
        modules = tmp / 'modules'
        (modules / 'zsh').mkdir(parents=True)
        shutil.copy2(ROOT / '.build/zsh/Src/zsh', shell)
        module = next((ROOT / '.build/modules').glob('zdraw.*'))
        shutil.copy2(module, modules)
        if args.mode == 'stock':
            shutil.copy2(ROOT / f'.build/zsh/Src/Modules/curses{module.suffix}', modules / 'zsh')
        config = (ROOT / '.build/zsh/config.h').read_text().splitlines()
        guarded = '#define HAVE_ZDRAW_SAFE_DELSCREEN 1' not in config
        print(json.dumps(run(args.cycles, args.seed, args.mode, shell, modules, guarded,
                             args.valgrind_log, args.timeout), indent=2))


if __name__ == '__main__':
    main()
