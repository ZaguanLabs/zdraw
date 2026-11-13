#!/usr/bin/env python3
"""Profile native operations and compare their timings and observable results."""
import argparse
import errno
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import pty
import select
import signal
import statistics
import struct
import subprocess
import termios
import time

ROOT = Path(__file__).resolve().parents[1]
WORKLOADS = ('textinfo', 'textinfo-long', 'textinfo-unicode', 'textpos-long',
             'grapheme-long', 'safe-query-long', 'safe-query-unicode', 'wrap-long',
             'spans', 'spans-distinct', 'spans-unicode', 'clip-long', 'safe-spans-unicode',
             'prepare', 'prepared', 'fill', 'snapshot', 'lookup')


def trial(shell, modules, workload, iterations, prefix=(), fixture=None, capture=None):
    read_fd, write_fd = os.pipe()
    os.set_inheritable(write_fd, True)
    pid, terminal = pty.fork()
    if pid == 0:
        os.close(read_fd)
        fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
        os.environ.update(TERM='xterm-256color',
                          LC_ALL=os.environ.get('ZDRAW_TEST_LOCALE', 'C.UTF-8'))
        os.environ.pop('LINES', None)
        os.environ.pop('COLUMNS', None)
        command = [*prefix, str(shell), '-df', str(fixture or ROOT / 'benchmarks/native.zsh'),
                   str(modules), workload, str(iterations), str(write_fd)]
        os.execvp(command[0], command)
    os.close(write_fd)
    report, output = bytearray(), bytearray()
    streams = {read_fd: report, terminal: output}
    reaped = False
    try:
        deadline = time.monotonic() + 120
        while streams:
            if time.monotonic() >= deadline:
                raise TimeoutError(f'{workload} exceeded 120 seconds')
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
        elapsed, observed = report.split(b'\n', 1)
        if capture:
            capture.with_suffix('.report').write_bytes(observed)
            capture.with_suffix('.terminal').write_bytes(output)
        return dict(ms_per_call=float(elapsed) * 1000 / iterations,
                    result_sha256=hashlib.sha256(observed).hexdigest(),
                    output_sha256=hashlib.sha256(output).hexdigest(),
                    output_bytes=len(output))
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
    parser.add_argument('--baseline-modules', type=Path,
                        help='alternate with this module directory and check exact outputs')
    parser.add_argument('--trials', type=int, default=7)
    parser.add_argument('--iterations', type=int, default=1000)
    parser.add_argument('--workloads', nargs='+', choices=WORKLOADS, default=WORKLOADS)
    parser.add_argument('--reuse', type=int, nargs='+',
                        help='instead measure spans vs prepare/draw/release at these reuse counts (1..256)')
    parser.add_argument('--label', default='working-tree')
    parser.add_argument('--callgrind', type=Path,
                        help='write instruction profiles here; requires an unstripped module')
    args = parser.parse_args()
    if not 1 <= args.trials <= 20 or not 1 <= args.iterations <= 10000:
        parser.error('trials must be 1..20 and iterations 1..10000')
    if args.reuse:
        if any(not 1 <= count <= 256 for count in args.reuse):
            parser.error('reuse counts must be 1..256')
        args.reuse = list(dict.fromkeys(args.reuse))
        args.workloads = [f'reuse-{backend}-{count}' for count in args.reuse
                          for backend in ('spans', 'prepared')]
    if args.callgrind:
        if args.baseline_modules:
            parser.error('profile and timing comparisons must be separate runs')
        args.callgrind.mkdir(parents=True, exist_ok=True)
    samples = {workload: [] for workload in args.workloads}
    baseline = {workload: [] for workload in args.workloads}
    for run in range(args.trials):
        for workload in (args.workloads if run % 2 == 0 else reversed(args.workloads)):
            prefix = []
            if args.callgrind:
                prefix = ['valgrind', '--tool=callgrind', '--collect-atstart=no',
                          '--toggle-collect=bin_zdraw',
                          f'--callgrind-out-file={args.callgrind / workload}.{run}']
            runs = [(args.modules, samples)]
            if args.baseline_modules:
                runs.append((args.baseline_modules, baseline))
                if run % 2:
                    runs.reverse()
            for modules, destination in runs:
                destination[workload].append(trial(args.shell, modules, workload,
                                                   args.iterations, prefix))
    results = {}
    for workload, values in samples.items():
        if len({v['result_sha256'] for v in values}) != 1:
            raise RuntimeError(f'unstable result: {workload}')
        results[workload] = dict(median_ms=statistics.median(v['ms_per_call'] for v in values),
                                 samples=values)
        if args.baseline_modules:
            for field in ('result_sha256', 'output_sha256', 'output_bytes'):
                if len({v[field] for v in [*values, *baseline[workload]]}) != 1:
                    raise RuntimeError(f'baseline mismatch: {workload} {field}')
            baseline_ms = statistics.median(v['ms_per_call'] for v in baseline[workload])
            results[workload].update(baseline_median_ms=baseline_ms,
                                     speedup=baseline_ms / results[workload]['median_ms'],
                                     baseline_samples=baseline[workload])
    for count in args.reuse or ():
        ordinary, prepared = (samples[f'reuse-{backend}-{count}'] for backend in ('spans', 'prepared'))
        for field in ('result_sha256', 'output_sha256', 'output_bytes'):
            if len({v[field] for v in [*ordinary, *prepared]}) != 1:
                raise RuntimeError(f'reuse backend mismatch: {count} {field}')
    print(json.dumps(dict(label=args.label, platform=platform.platform(),
                          shell=subprocess.check_output([args.shell, '--version'], text=True).strip(),
                          locale=os.environ.get('ZDRAW_TEST_LOCALE', 'C.UTF-8'),
                          trials=args.trials, iterations=args.iterations,
                          reuse_counts=args.reuse,
                          instrumented=bool(args.callgrind), results=results), indent=2))


if __name__ == '__main__':
    main()
