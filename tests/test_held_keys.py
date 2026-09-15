"""R3: existing zdraw events compose into bounded, explicit held-key state."""
import errno
import fcntl
import os
import pty
import select
import signal
import struct
import termios
import time
import unittest

import test_features

ROOT, ZSH = test_features.ROOT, test_features.ZSH


def wait_child(pid, timeout=5, pump=None):
    """Keep PTY output moving while waiting; never block indefinitely on exit."""
    deadline = time.monotonic() + timeout
    while True:
        child, status = os.waitpid(pid, os.WNOHANG)
        if child:
            return status
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f'Child {pid} did not exit within {timeout}s')
        if pump:
            pump(min(0.02, remaining))
        else:
            time.sleep(min(0.02, remaining))


def kill_child(pid):
    try:
        os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    try:
        wait_child(pid, timeout=2)
    except ChildProcessError:
        pass  # Already reaped after an unexpected early exit.


class HeldKeyTests(unittest.TestCase):
    def test_child_exit_deadline(self):
        pid = os.fork()
        if pid == 0:
            time.sleep(60)
            os._exit(0)
        try:
            with self.assertRaises(TimeoutError):
                wait_child(pid, timeout=0.05)
        finally:
            kill_child(pid)

    def test_passive_policy_and_bounds(self):
        output = test_features.FeatureTests().run_shell(r'''
            source "${module_path[1]:h:h}/examples/held-keys.zsh" || exit 1
            zmodload -e zdraw && exit 2
            typeset -A held event
            typeset held_mode held_note
            typeset -i held_focused i
            held-demo-start legacy || exit 3
            [[ $held_mode == legacy && ${#held} == 0 ]] || exit 4
            held_mode=enhanced
            for ((i=0;i<64;i++)); do
                event=(type key source kitty supported yes key "U+$i" action press)
                held-demo-event || exit 5
            done
            (( ${#held} == 64 )) || exit 6
            held-demo-event
            (( ${#held} == 64 )) || exit 7
            event[key]=U+65
            held-demo-event
            (( ${#held} == 0 )) || exit 8
            event[action]=repeat
            held-demo-event
            (( ${#held} == 0 )) || exit 9
            event[action]=release
            held-demo-event
            (( ${#held} == 0 )) || exit 10
            print PASS
        ''')
        self.assertEqual(output, 'PASS\n')

    def session(self, scenario):
        control_r, control_w = os.pipe()
        report_r, report_w = os.pipe()
        pid, terminal = pty.fork()
        if pid == 0:
            os.close(control_w)
            os.close(report_r)
            os.set_inheritable(control_r, True)
            os.set_inheritable(report_w, True)
            fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
            os.environ.update(TERM='xterm-256color', LC_ALL='C.UTF-8')
            os.environ.pop('LINES', None)
            os.environ.pop('COLUMNS', None)
            os.execl(ZSH, ZSH, '-df', str(ROOT / 'tests/held-keys.zsh'),
                     str(ROOT / '.build/modules'), str(report_w), str(control_r), scenario)
        os.close(control_r)
        os.close(report_w)
        payloads = {
            b'focus-query': b'' if scenario == 'focus-timeout' else b'\x1b[?1004;2$y',
            b'keyboard-query': b'' if scenario == 'keyboard-timeout' else b'\x1b[?0u',
            b'late-focus': b'\x1b[?1004;2$y', b'late-keyboard': b'\x1b[?0u',
            b'multi': b'\x1b[119;2:1u\x1b[100;1:1u\x1b[119;2:2u\x1b[119;1:3u\x1b[100;1:3u',
            b'focus-loss': b'\x1b[119u\x1b[O\x1b[100u\x1b[I\x1b[119;1:2u\x1b[119u',
            b'legacy-arrow': b'\x1bOA',
            b'after-resize': b'\x1b[119;1:2u\x1b[119u',
            b'resumed': b'\x1b[I\x1b[119;1:2u\x1b[119u',
            b'malformed': b'\x1b[119;1:4u\x1b[119;1:2u\x1b[119u',
            b'partial': b'\x1b[119;',
        }
        pending, screen = bytearray(), bytearray()
        original, reaped, resumed = None, False, False
        def drain(fd):
            try:
                data = os.read(fd, 65536)
            except OSError as error:
                if fd != terminal or error.errno != errno.EIO:
                    raise
                data = b''
            if fd == report_r:
                self.assertTrue(data, bytes(screen[-3000:]))
                pending.extend(data)
            else:
                screen.extend(data)
        try:
            while True:
                deadline = time.monotonic() + 10
                while b'\n' not in pending:
                    remaining = deadline-time.monotonic()
                    self.assertGreater(remaining, 0, bytes(screen[-3000:]))
                    for fd in select.select([terminal, report_r], [], [], remaining)[0]:
                        drain(fd)
                step, _, rest = pending.partition(b'\n')
                pending[:] = rest
                step = bytes(step)
                if step == b'done':
                    break
                if step == b'baseline':
                    original = termios.tcgetattr(terminal)
                elif step in (b'suspended', b'cleaned'):
                    self.assertEqual(termios.tcgetattr(terminal), original)
                elif step == b'resize':
                    fcntl.ioctl(terminal, termios.TIOCSWINSZ, struct.pack('HHHH', 18, 60, 0, 0))
                    os.kill(pid, signal.SIGWINCH)
                else:
                    os.write(terminal, payloads[step])
                if step == b'resumed':
                    resumed = True  # Explicit resume restores the virtual screen.
                if not resumed:
                    self.assertNotIn(b'HELD_UNPRESENTED', screen)
                os.write(control_w, b'continue\n')
            def pump(timeout):
                if select.select([terminal], [], [], timeout)[0]:
                    drain(terminal)
            status = wait_child(pid, pump=pump)
            reaped = True
            self.assertEqual(os.waitstatus_to_exitcode(status), 0, bytes(screen[-3000:]))
            self.assertEqual(termios.tcgetattr(terminal), original)
            while select.select([terminal], [], [], 0)[0]:
                before = len(screen)
                drain(terminal)
                if len(screen) == before:
                    break
            pushes = 2 if scenario == 'enhanced' else 0
            focuses = 2 if scenario == 'enhanced' else int(scenario == 'keyboard-timeout')
            self.assertEqual(screen.count(b'\x1b[>27u'), pushes)
            self.assertEqual(screen.count(b'\x1b[<u'), pushes)
            self.assertEqual(screen.count(b'\x1b[?1004h'), focuses)
            self.assertEqual(screen.count(b'\x1b[?1004l'), focuses)
        finally:
            try:
                if not reaped:
                    kill_child(pid)
            finally:
                for fd in (terminal, control_w, report_r):
                    os.close(fd)

    def test_multiple_keys_focus_resize_suspend_and_cleanup(self):
        self.session('enhanced')

    def test_failed_negotiation_and_late_replies(self):
        for scenario in ('focus-timeout', 'keyboard-timeout'):
            with self.subTest(scenario=scenario):
                self.session(scenario)

    def test_inspector_quit_signal_and_job_control(self):
        for scenario in ('legacy', 'partial-term', 'suspend-int'):
            with self.subTest(scenario=scenario):
                gate_r, gate_w = os.pipe()
                pid, terminal = pty.fork()
                if pid == 0:
                    os.close(gate_w)
                    fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 80, 0, 0))
                    os.environ.update(TERM='xterm-256color', LC_ALL='C.UTF-8')
                    os.environ.pop('LINES', None)
                    os.environ.pop('COLUMNS', None)
                    os.read(gate_r, 1)
                    os.close(gate_r)
                    args = ['--legacy'] if scenario == 'legacy' else []
                    os.execl(ZSH, ZSH, '-df', str(ROOT / 'examples/held-keys.zsh'), *args)
                os.close(gate_r)
                original = termios.tcgetattr(terminal)
                os.write(gate_w, b'1')
                os.close(gate_w)
                screen, reaped = bytearray(), False
                def read_screen(timeout=0.05):
                    if select.select([terminal], [], [], timeout)[0]:
                        try:
                            screen.extend(os.read(terminal, 65536))
                        except OSError as error:
                            if error.errno != errno.EIO:
                                raise
                def wait_for(marker):
                    deadline = time.monotonic() + 5
                    while marker not in screen and time.monotonic() < deadline:
                        read_screen()
                    self.assertIn(marker, screen, bytes(screen[-2000:]))
                try:
                    wait_for(b'held-key inspector')
                    if scenario == 'legacy':
                        self.assertNotIn(b'\x1b[?1004$p', screen)
                        os.write(terminal, b'q')
                        expected = 0
                    else:
                        wait_for(b'\x1b[?1004$p')
                        os.write(terminal, b'\x1b[?1004;2$y')
                        wait_for(b'\x1b[?u')
                        os.write(terminal, b'\x1b[?0u')
                        wait_for(b'\x1b[>27u')
                        os.write(terminal, b'\x1b[119u')
                        wait_for(b'U+0077')
                        if scenario == 'partial-term':
                            os.write(terminal, b'\x1b[119;')
                            os.kill(pid, signal.SIGTERM)
                            expected = 143
                        else:
                            os.kill(pid, signal.SIGTSTP)
                            deadline = time.monotonic() + 5
                            stopped = False
                            while time.monotonic() < deadline:
                                read_screen()
                                child, state = os.waitpid(pid, os.WNOHANG | os.WUNTRACED)
                                if child:
                                    self.assertTrue(os.WIFSTOPPED(state), bytes(screen[-2000:]))
                                    stopped = True
                                    break
                            self.assertTrue(stopped, bytes(screen[-2000:]))
                            self.assertEqual(termios.tcgetattr(terminal), original)
                            screen.clear()
                            os.kill(pid, signal.SIGCONT)
                            wait_for(b'Resumed; held state cleared.')
                            os.kill(pid, signal.SIGINT)
                            expected = 130
                    deadline = time.monotonic() + 5
                    while time.monotonic() < deadline:
                        read_screen()
                        child, state = os.waitpid(pid, os.WNOHANG)
                        if child:
                            reaped = True
                            self.assertEqual(os.waitstatus_to_exitcode(state), expected, bytes(screen[-2000:]))
                            break
                    self.assertTrue(reaped, bytes(screen[-2000:]))
                    self.assertEqual(termios.tcgetattr(terminal), original)
                finally:
                    try:
                        if not reaped:
                            kill_child(pid)
                    finally:
                        os.close(terminal)


if __name__ == '__main__':
    unittest.main(verbosity=2)
