"""Optional Zsh components: pure style/state contracts and retained cells."""
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


class UITests(unittest.TestCase):
    def test_headless_styles_and_state(self):
        test_clipping.ClippingTests().headless(fixture='ui-style.zsh', marker='UI STYLE PASS')

    def test_headless_diagnostics(self):
        test_clipping.ClippingTests().headless(fixture='ui-diagnostics.zsh', marker='UI DIAGNOSTICS PASS')

    def test_component_diagnostics(self):
        test_clipping.ClippingTests().headless(
            fixture='ui-component-diagnostics.zsh', marker='UI COMPONENT DIAGNOSTICS PASS')

    def test_headless_layout(self):
        test_clipping.ClippingTests().headless(fixture='ui-layout.zsh', marker='UI LAYOUT PASS')

    def test_components(self):
        for mode in ('wide', 'ascii'):
            with self.subTest(mode=mode):
                drawing_session(self, mode, fixture='ui.zsh', marker=b'UI PASS')

    def test_table(self):
        drawing_session(self, 'wide', fixture='ui-table.zsh', marker=b'UI TABLE PASS')

    def test_presentation_components(self):
        drawing_session(self, 'wide', fixture='ui-presentation.zsh', marker=b'UI PRESENTATION PASS')

    def test_gallery_interaction_and_resize(self):
        self.interactive_example('gallery')

    def test_color_studio_interaction_and_resize(self):
        self.interactive_example('color-studio')

    def test_list_detail_recipe(self):
        self.interactive_example('list-detail')

    def test_table_inspector_recipe(self):
        self.interactive_example('table-inspector')

    def test_task_monitor_recipe(self):
        self.interactive_example('task-monitor')

    def test_synchronized_task_monitor_recipe(self):
        self.interactive_example('task-monitor', '--sync')

    def interactive_example(self, example, *options):
        control_r, control_w = os.pipe()
        report_r, report_w = os.pipe()
        pid, terminal = pty.fork()
        if pid == 0:
            os.close(control_w)
            os.close(report_r)
            os.set_inheritable(control_r, True)
            os.set_inheritable(report_w, True)
            fcntl.ioctl(0, termios.TIOCSWINSZ, struct.pack('HHHH', 24, 100, 0, 0))
            os.environ.update(TERM='xterm-256color', LC_ALL='C.UTF-8')
            for name in ('LINES', 'COLUMNS', 'NO_COLOR'):
                os.environ.pop(name, None)
            os.execl(test_features.ZSH, test_features.ZSH, '-df',
                     str(test_features.ROOT / 'tests/gallery.zsh'),
                     str(report_w), str(control_r), os.environ.get('ZDRAW_GALLERY_CAPTURE', ''), example, *options)
        os.close(control_r)
        os.close(report_w)
        pending, screen = bytearray(), bytearray()
        reaped = False

        def report():
            deadline = time.monotonic() + 10
            watched = [terminal, report_r]
            while b'\n' not in pending:
                remaining = deadline - time.monotonic()
                self.assertGreater(remaining, 0, bytes(screen[-4000:]))
                for fd in select.select(watched, [], [], remaining)[0]:
                    try:
                        data = os.read(fd, 65536)
                    except OSError as exc:
                        if exc.errno != errno.EIO:
                            raise
                        data = b''
                    if not data and fd == terminal:
                        watched.remove(terminal)
                        continue
                    self.assertTrue(data, bytes(screen[-4000:]))
                    (screen if fd == terminal else pending).extend(data)
            line, _, rest = pending.partition(b'\n')
            pending[:] = rest
            return line.decode().split()

        def advance(key=b'', size=None):
            if key:
                os.write(terminal, key)
            if size:
                fcntl.ioctl(terminal, termios.TIOCSWINSZ, struct.pack('HHHH', *size, 0, 0))
                os.kill(pid, signal.SIGWINCH)
            os.write(control_w, b'continue\n')
            return report()

        try:
            self.assertEqual(report(), ['baseline'])
            original = termios.tcgetattr(terminal)
            if example == 'color-studio':
                self.assertEqual(advance(), 'colors 24 100 dark 256'.split())
                self.assertEqual(advance(b't'), 'colors 24 100 light 256'.split())
                self.assertEqual(advance(b'm')[-1], 'mono')
                self.assertEqual(advance(size=(10, 28))[1:3], ['10', '28'])
                self.assertEqual(advance(size=(4, 12))[1:3], ['4', '12'])
                self.assertEqual(advance(b'm')[-1], '256')
                self.assertEqual(advance(size=(24, 100))[1:3], ['24', '100'])
                self.assertEqual(advance(b'q'), ['done'])
                _, status = os.waitpid(pid, 0)
                reaped = True
                self.assertEqual(os.waitstatus_to_exitcode(status), 0, bytes(screen[-4000:]))
                self.assertEqual(termios.tcgetattr(terminal), original)
                return
            if example == 'task-monitor':
                self.assertEqual(advance(), 'monitor 24 100 1 0 0 0 1 dark auto 256 1 0'.split())
                if options:
                    self.assertNotIn(b'\x1b[?2026h', screen)
                    self.assertEqual(advance(b'\x1b[?2026;2$y'), 'monitor 24 100 1 0 0 0 1 dark auto 256 1 0'.split())
                self.assertEqual(advance()[5], '1')  # idle timeout advances simulated work
                self.assertEqual(advance(b' ')[4:6], ['1', '1'])
                self.assertEqual(advance(b'n')[5], '2')
                self.assertEqual(advance(b'\t')[3], '2')
                self.assertEqual(advance(b'j')[7], '2')
                self.assertEqual(advance(b'3')[3], '3')
                self.assertEqual(advance(b'g')[9:], ['ascii', '256', '3', '4'])
                self.assertEqual(advance(b'm')[10], 'mono')
                self.assertEqual(advance(size=(10, 28))[3], '3')
                self.assertEqual(advance(size=(24, 100))[11:], ['3', '4'])
                self.assertEqual(advance(b'm')[10], '256')
                self.assertEqual(advance(b'2')[3], '2')
                self.assertEqual(advance(b't')[8], 'light')
                self.assertEqual(advance(size=(10, 28))[1:3], ['10', '28'])
                self.assertEqual(advance(b'1')[3], '1')
                self.assertEqual(advance(size=(4, 12))[1:3], ['4', '12'])
                self.assertEqual(advance(size=(24, 100))[4:6], ['1', '2'])
                self.assertEqual(advance(b'r')[5:7], ['0', '0'])
                for tick in range(1, 101):
                    result = advance(b'n')
                    self.assertEqual(result[5], str(tick))
                self.assertEqual(result[6], '3')
                self.assertEqual(result[11:], ['96', '100'])
                self.assertEqual(advance(b'3')[3], '3')
                self.assertEqual(advance(size=(12, 40))[11:], ['96', '100'])
                self.assertEqual(advance(b'r')[11:], ['1', '0'])
                self.assertEqual(advance(b'q'), ['done'])
                _, status = os.waitpid(pid, 0)
                reaped = True
                self.assertEqual(os.waitstatus_to_exitcode(status), 0, bytes(screen[-4000:]))
                self.assertEqual(termios.tcgetattr(terminal), original)
                if options:
                    self.assertGreater(screen.count(b'\x1b[?2026h'), 100)
                    self.assertEqual(screen.count(b'\x1b[?2026h'), screen.count(b'\x1b[?2026l'))
                return
            if example == 'table-inspector':
                self.assertEqual(advance(), 'inspector 24 100 split table dark 1 0'.split())
                self.assertEqual(advance(b'j')[6], '2')
                self.assertEqual(advance(b'e')[6:], ['0', '1'])
                self.assertEqual(advance(b'e')[6:], ['1', '0'])
                self.assertEqual(advance(size=(18, 44))[3:5], ['single', 'table'])
                self.assertEqual(advance(b'\r')[4], 'detail')
                self.assertEqual(advance(b'j')[6], '2')
                self.assertEqual(advance(b'\x1b')[4], 'table')
                self.assertEqual(advance(size=(18, 72))[3:5], ['single', 'table'])
                self.assertEqual(advance(size=(24, 120))[3], 'split')
                self.assertEqual(advance(b't')[5], 'light')
                self.assertEqual(advance(size=(8, 24))[3], 'single')
                self.assertEqual(advance(b'\x1bOF')[6], '5')
                self.assertEqual(advance(size=(4, 12))[3], 'tiny')
                self.assertEqual(advance(size=(24, 100))[6], '5')
                self.assertEqual(advance(b'q'), ['done'])
                _, status = os.waitpid(pid, 0)
                reaped = True
                self.assertEqual(os.waitstatus_to_exitcode(status), 0, bytes(screen[-4000:]))
                self.assertEqual(termios.tcgetattr(terminal), original)
                return
            if example == 'list-detail':
                self.assertEqual(advance(), 'recipe 24 100 split list dark 1'.split())
                self.assertEqual(advance(b'j')[-1], '2')
                self.assertEqual(advance(size=(18, 44))[3:5], ['single', 'list'])
                self.assertEqual(advance(b'\r')[4], 'detail')
                self.assertEqual(advance(b'j')[-1], '3')
                self.assertEqual(advance(b'\x1b')[4], 'list')
                self.assertEqual(advance(b'\r')[4], 'detail')
                self.assertEqual(advance(size=(24, 100))[3:], ['split', 'detail', 'dark', '3'])
                self.assertEqual(advance(b't')[5], 'light')
                self.assertEqual(advance(size=(18, 44))[3:5], ['single', 'detail'])
                self.assertEqual(advance(b'\r')[4], 'list')
                # Short viewports scroll to the last item, then preserve it
                # through collapse and expansion of the entire layout.
                self.assertEqual(advance(size=(8, 24))[3], 'single')
                self.assertEqual(advance(b'\x1bOF')[-1], '5')
                self.assertEqual(advance(size=(4, 12))[3], 'tiny')
                self.assertEqual(advance(size=(24, 100))[-1], '5')
                self.assertEqual(advance(b'q'), ['done'])
                _, status = os.waitpid(pid, 0)
                reaped = True
                self.assertEqual(os.waitstatus_to_exitcode(status), 0, bytes(screen[-4000:]))
                self.assertEqual(termios.tcgetattr(terminal), original)
                return
            self.assertEqual(advance(), 'frame 24 100 dark 256 1 0 0 0 focus 1'.split())
            self.assertEqual(advance(b'j')[-1], '2')
            self.assertEqual(advance(b't')[3], 'light')
            for border in ('2', '3', '4', '1'):
                self.assertEqual(advance(b'b')[5], border)
            self.assertEqual(advance(b'd')[6], '1')
            self.assertEqual(advance(b'\t')[-2], 'inactive')
            self.assertEqual(advance(b'j')[-1], '2')  # inactive input remains caller policy
            self.assertEqual(advance(b'x')[-2], 'disabled')
            self.assertEqual(advance(b'e')[-1], '0')
            self.assertEqual(advance(b'n')[8], '1')
            self.assertEqual(advance(b'm')[4], 'mono')
            self.assertEqual(advance(size=(6, 16))[1:3], ['6', '16'])
            self.assertEqual(advance(size=(30, 120))[1:3], ['30', '120'])
            self.assertEqual(advance(b'q'), ['done'])
            _, status = os.waitpid(pid, 0)
            reaped = True
            self.assertEqual(os.waitstatus_to_exitcode(status), 0, bytes(screen[-4000:]))
            self.assertEqual(termios.tcgetattr(terminal), original)
        finally:
            if not reaped:
                os.kill(pid, signal.SIGKILL)
                os.waitpid(pid, 0)
            for fd in (terminal, control_w, report_r):
                os.close(fd)


if __name__ == '__main__':
    unittest.main(verbosity=2)
