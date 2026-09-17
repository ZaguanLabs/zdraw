"""Pane selection source ranges, native styling, routing and lifecycle."""
import unittest
import errno
import fcntl
import os
import pty
import select
import signal
import struct
import termios
import time
import test_clipping
import test_features
from test_drawing import drawing_session


class TextSelectionTests(unittest.TestCase):
    def test_ranges_and_events(self):
        test_clipping.ClippingTests().headless(fixture='text-selection.zsh', marker='TEXT SELECTION PASS')

    def test_styles_and_mouse_configuration(self):
        drawing_session(self, 'wide', fixture='text-selection-draw.zsh',
                        marker=b'TEXT SELECTION DRAW PASS')

    def test_example_real_mouse_routing_resize_and_cleanup(self):
        self.example('--mouse')

    def test_example_monochrome(self):
        self.example('--mouse', '--mono')

    def test_example_disabled_mouse(self):
        self.example()

    def example(self, *options):
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
            os.execl(test_features.ZSH, test_features.ZSH, '-df',
                     str(test_features.ROOT / 'tests/text-selection-example.zsh'),
                     str(report_w), str(control_r), *options)
        os.close(control_r)
        os.close(report_w)
        pending, screen = bytearray(), bytearray()
        reaped = False

        def report():
            deadline = time.monotonic() + 15
            while b'\n' not in pending:
                self.assertLess(time.monotonic(), deadline, bytes(screen[-4000:]))
                for fd in select.select([report_r, terminal], [], [], .1)[0]:
                    try:
                        data = os.read(fd, 65536)
                    except OSError as exc:
                        if exc.errno != errno.EIO:
                            raise
                        data = b''
                    if fd == report_r:
                        self.assertTrue(data, bytes(screen[-4000:]))
                        pending.extend(data)
                    else:
                        screen.extend(data)
            line, _, rest = pending.partition(b'\n')
            pending[:] = rest
            return line.decode().split()

        def advance(data=b'', size=None):
            if data:
                os.write(terminal, data)
            if size:
                fcntl.ioctl(terminal, termios.TIOCSWINSZ, struct.pack('HHHH', *size, 0, 0))
                os.kill(pid, signal.SIGWINCH)
            os.write(control_w, b'continue\n')
            return report()

        def mouse(x, y, code=0, release=False):
            return advance(f'\x1b[<{code};{x+1};{y+1}{"m" if release else "M"}'.encode())

        try:
            self.assertEqual(report(), ['baseline'])
            original = termios.tcgetattr(terminal)
            self.assertEqual(advance()[:3], ['frame', '24', '80'])
            if '--mouse' in options:
                # First read enables tracking. Each observer pauses between reads.
                self.assertEqual(advance()[:3], ['frame', '24', '80'])
                self.assertEqual(mouse(3, 2)[4:8], ['1', '1', '1', '0'])
                self.assertEqual(mouse(75, 5, 32)[4:8], ['1', '1', '1', '0'])
                self.assertEqual(mouse(75, 5, release=True)[4:8], ['0', '1', '1', '0'])
                copied = advance(b'c')
                self.assertNotIn('SIDEBAR', ' '.join(copied[10:]))
                self.assertIn('pane', ' '.join(copied[10:]))
                self.assertEqual(advance(b'\x1b')[5], '0')
                # Press outside, move inside: no selection, sidebar receives click.
                self.assertEqual(mouse(75, 4)[7], '1')
                self.assertEqual(mouse(4, 2, 32)[5], '0')
                self.assertEqual(mouse(4, 2, release=True)[5], '0')
                mouse(3, 2)
                self.assertEqual(advance(b'a')[4:6], ['0', '0'])
                self.assertEqual(mouse(75, 5, release=True)[7], '1')
                mouse(3, 2)
                self.assertEqual(advance(b'm')[4:6], ['0', '0'])
                self.assertEqual(advance(b'm')[8:10], ['0', '1'])
                mouse(3, 2)
                self.assertEqual(advance(b'j')[4:6], ['0', '0'])
                mouse(75, 5, release=True)
                self.assertEqual(advance(b'd')[9], '0')
                self.assertEqual(advance(b'd')[9], '1')
            else:
                self.assertIn('pane', ' '.join(advance(b'f')[10:]))
            resized = advance(size=(12, 32))
            self.assertEqual(resized[1:3], ['12', '32'])
            self.assertEqual(resized[4:6], ['0', '0'])
            # A second curses resize notification may follow the geometry event.
            advance()
            self.assertEqual(advance(size=(17, 42))[1:3], ['17', '42'])
            advance()
            if '--mouse' in options:
                mouse(7, 2)
                mouse(36, 9, 32)
                mouse(36, 9, release=True)
                self.assertIn('copied text', ' '.join(advance(b'c')[10:]))
            self.assertEqual(advance(size=(6, 20))[1:3], ['6', '20'])
            advance()
            self.assertEqual(advance(size=(24, 80))[1:3], ['24', '80'])
            advance()
            self.assertEqual(advance(b'q'), ['done'])
            _, status = os.waitpid(pid, 0)
            reaped = True
            self.assertEqual(os.waitstatus_to_exitcode(status), 0, bytes(screen[-4000:]))
            self.assertEqual(termios.tcgetattr(terminal), original)
            if '--mouse' in options:
                self.assertIn(b'\x1b[?1006;1000h', screen)
                self.assertIn(b'\x1b[?1006;1000l', screen)
        finally:
            if not reaped:
                os.kill(pid, signal.SIGKILL)
                os.waitpid(pid, 0)
            for fd in (control_w, report_r, terminal):
                os.close(fd)


if __name__ == '__main__':
    unittest.main()
