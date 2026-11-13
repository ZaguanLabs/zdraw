#!/usr/bin/env python3
"""Measure input-to-present completion under controlled PTY output pressure."""
import argparse
import errno
import fcntl
import json
import math
import os
from pathlib import Path
import pty
import select
import signal
import statistics
import struct
import termios
import time

ROOT = Path(__file__).resolve().parents[1]


def trial(frames, batch, pause, resize):
    report_r, report_w = os.pipe()
    go_r, go_w = os.pipe()
    pid, terminal = pty.fork()
    if pid == 0:
        os.close(report_r)
        os.close(go_w)
        os.read(go_r, 1)
        os.close(go_r)
        os.set_inheritable(report_w, True)
        os.environ.update(TERM='xterm-256color', LC_ALL='C.UTF-8')
        for key in ('LINES', 'COLUMNS'):
            os.environ.pop(key, None)
        shell = str(ROOT / '.build/zsh/Src/zsh')
        os.execl(shell, shell, '-df', str(ROOT / 'benchmarks/latency.zsh'),
                 str(ROOT / '.build/modules'), str(report_w), str(frames))
    os.close(report_w)
    os.close(go_r)
    pending, output = bytearray(), bytearray()
    latencies, sent_at = [], []
    submitted = completed = 0
    release = 0
    ready = done = reaped = False
    watched = [report_r, terminal]
    deadline = time.monotonic() + 120
    try:
        fcntl.ioctl(terminal, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
        original = termios.tcgetattr(terminal)
        os.write(go_w, b'!')
        while watched:
            now = time.monotonic()
            if now >= deadline:
                raise TimeoutError(f'latency test stalled at {completed}/{frames}')
            if ready and completed == submitted and submitted < frames:
                count = min(batch, frames-submitted)
                if resize:
                    size = (40, 120) if (submitted // batch) % 2 == 0 else (24, 80)
                    fcntl.ioctl(terminal, termios.TIOCSWINSZ, struct.pack('HHHH', *size, 0, 0))
                    os.kill(pid, signal.SIGWINCH)
                sent_at.extend([now] * count)
                os.write(terminal, bytes(97 + i % 26 for i in range(submitted, submitted+count)))
                submitted += count
                release = now + pause
            # Read completion reports even while output draining is withheld.
            active = [fd for fd in watched if fd != terminal or now >= release]
            wait = min(.1, max(0, release-now)) if now < release else .1
            for fd in select.select(active, [], [], wait)[0]:
                try:
                    chunk = os.read(fd, 65536)
                except OSError as error:
                    if fd != terminal or error.errno != errno.EIO:
                        raise
                    chunk = b''
                if not chunk:
                    watched.remove(fd)
                    continue
                if fd == terminal:
                    output.extend(chunk)
                    continue
                pending.extend(chunk)
                while b'\n' in pending:
                    line, _, rest = pending.partition(b'\n')
                    pending[:] = rest
                    if line == b'ready':
                        ready = True
                    elif line == b'done':
                        done = True
                    else:
                        expected = f'{completed} {chr(97 + completed % 26)}'.encode()
                        if line != expected:
                            raise RuntimeError(f'input reordered/lost: {line!r}, expected {expected!r}')
                        latencies.append((time.monotonic()-sent_at[completed])*1000)
                        completed += 1
        _, status = os.waitpid(pid, 0)
        reaped = True
        if os.waitstatus_to_exitcode(status) or not done or completed != frames:
            raise RuntimeError(f'incomplete trial: {output[-2000:]!r}')
        if termios.tcgetattr(terminal) != original:
            raise RuntimeError('terminal modes were not restored')
        ordered = sorted(latencies)
        return dict(frames=frames, batch=batch, output_pause_ms=pause*1000,
                    resize=resize, terminal_restored=True, output_bytes=len(output),
                    median_ms=statistics.median(latencies),
                    p95_ms=ordered[math.ceil(.95*frames)-1],
                    p99_ms=ordered[math.ceil(.99*frames)-1], maximum_ms=max(latencies),
                    samples_ms=latencies)
    finally:
        if not reaped:
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            os.waitpid(pid, 0)
        for fd in (terminal, report_r, go_w):
            os.close(fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frames', type=int, default=200)
    args = parser.parse_args()
    if not 40 <= args.frames <= 1000:
        parser.error('frames must be 40..1000')
    print(json.dumps(dict(note='Input injection to observed present completion in a drained/paused local PTY; includes scheduling and queued keys, excludes emulator painting. Delays are injected, not a hard responsiveness guarantee.',
                          results=[trial(args.frames, *case) for case in
                                   ((1, 0, False), (8, 0, False), (8, .05, False), (8, .05, True))]), indent=2))


if __name__ == '__main__':
    main()
