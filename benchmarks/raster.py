#!/usr/bin/env python3
"""Compare native and reference raster/packing on the same captured camera sweep."""
import argparse
import errno
import fcntl
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import pty
import select
import signal
import struct
import subprocess
import tempfile
import termios
import time

ROOT = Path(__file__).resolve().parents[1]


def trial(shell, modules, fixture, repetitions, backend, mode, width, height):
    read_fd, write_fd = os.pipe()
    os.set_inheritable(write_fd, True)
    pid, terminal = pty.fork()
    if pid == 0:
        os.close(read_fd)
        fcntl.ioctl(0, termios.TIOCSWINSZ,
                    struct.pack('HHHH', max(24, (height+1)//2), max(80, width), 0, 0))
        os.environ.update(TERM='vt100' if mode == 'mono' else 'xterm-256color',
                          LC_ALL=os.environ.get('ZDRAW_TEST_LOCALE', 'C.UTF-8'))
        os.environ.pop('LINES', None)
        os.environ.pop('COLUMNS', None)
        command = [str(shell), '-df', str(ROOT / 'benchmarks/raster.zsh'),
                   str(modules), str(fixture), str(repetitions), str(write_fd), backend, mode]
        os.execv(command[0], command)
    os.close(write_fd)
    report, output = bytearray(), bytearray()
    streams = {read_fd: report, terminal: output}
    reaped = False
    try:
        deadline = time.monotonic() + 180
        while streams:
            if time.monotonic() >= deadline:
                raise TimeoutError('Raster benchmark exceeded 180 seconds')
            for fd in select.select(list(streams), [], [], 0.1)[0]:
                try:
                    data = os.read(fd, 65536)
                except OSError as error:
                    if error.errno != errno.EIO or fd != terminal:
                        raise
                    data = b''
                if data:
                    streams[fd].extend(data)
                else:
                    del streams[fd]
        _, status = os.waitpid(pid, 0)
        reaped = True
        if os.waitstatus_to_exitcode(status):
            raise RuntimeError(output.decode(errors='replace'))
        values = list(map(float, report.split()))
        count = int(values[0])
        result = dict(zip(('split_ms', 'clear_ms', 'raster_ms', 'pack_ms', 'present_ms', 'total_ms'),
                          (value*1000/count for value in values[1:])))
        result.update(frames=count, output_bytes=len(output),
                      output_sha256=hashlib.sha256(output).hexdigest(), backend=backend, mode=mode)
        return result
    finally:
        if not reaped:
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            os.waitpid(pid, 0)
        os.close(read_fd)
        os.close(terminal)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shell', type=Path, default=ROOT / '.build/zsh/Src/zsh')
    parser.add_argument('--modules', type=Path, default=ROOT / '.build/modules')
    parser.add_argument('--repetitions', type=int, default=5)
    parser.add_argument('--backends', nargs='+', choices=('native', 'reference'), default=['native', 'reference'])
    parser.add_argument('--modes', nargs='+', choices=('headless', 'half', 'ascii', 'mono'), default=['headless', 'half'])
    args = parser.parse_args()
    if not 1 <= args.repetitions <= 20:
        parser.error('repetitions must be 1..20')
    fixture = json.loads(gzip.decompress((ROOT / 'benchmarks/fixtures/raster-sweep.json.gz').read_bytes()))
    results = []
    for size in fixture['sizes']:
        with tempfile.NamedTemporaryFile(mode='w', dir=ROOT / '.build', suffix='.raster') as source:
            source.write(f"{size['width']} {size['height']}\n")
            for frame in size['frames']:
                source.write(' '.join(str(value) for triangle in frame['triangles'] for value in triangle) + '\n')
            source.flush()
            for mode in args.modes:
                for backend in args.backends:
                    result = trial(args.shell.resolve(), args.modules.resolve(), source.name,
                                   args.repetitions, backend, mode, size['width'], size['height'])
                    result.update(width=size['width'], height=size['height'])
                    results.append(result)
                compared = [r for r in results if r['width'] == size['width'] and r['mode'] == mode]
                if len({r['output_sha256'] for r in compared}) != 1:
                    raise RuntimeError(f"Native/reference terminal output mismatch: {size['width']} {mode}")
    print(json.dumps(dict(platform=platform.platform(),
                          shell=subprocess.check_output([args.shell, '--version'], text=True).strip(),
                          locale=os.environ.get('ZDRAW_TEST_LOCALE', 'C.UTF-8'),
                          note='Captured screen-space geometry; transforms/game loop excluded. Drained PTY does not measure terminal emulator painting.',
                          results=results), indent=2))


if __name__ == '__main__':
    main()
