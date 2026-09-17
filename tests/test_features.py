"""Headless discovery, feature lifecycle, and real alternative module builds."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / '.build/zsh'
ZSH = shutil.which(os.environ.get('ZSH_TEST_SHELL', str(BUILD / 'Src/zsh')))


class FeatureTests(unittest.TestCase):
    def run_shell(self, script, modules=None, terminal=None):
        env = os.environ.copy()
        if terminal is None:
            env.pop('TERM', None)
        else:
            env['TERM'] = terminal
        result = subprocess.run(
            [ZSH, '-dfc', 'module_path=("$1"); shift\n' + script,
             'features-test', str(modules or ROOT / '.build/modules')],
            env=env, stdin=subprocess.DEVNULL, start_new_session=True,
            capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, '')
        return result.stdout

    def test_headless_discovery(self):
        script = '''
            zmodload zdraw || exit 1
            zmodload -F -e zdraw +p:zdraw_features || exit 2
            [[ ${(t)zdraw_features} == array-readonly-* ]] || exit 3
            (( ${#zdraw_windows} == 0 )) || exit 4
            typeset -a snapshot=("${zdraw_features[@]}")
            typeset -a unique=("${(@u)snapshot}")
            (( ${#snapshot} == ${#unique} )) || exit 5
            (( ! ${zdraw_features[(Ie)unrecognised_feature]} )) || exit 6
            [[ ${zdraw_features[(Ie)default_colors]} -gt 0 ]] && has_default=1 || has_default=0
            [[ ${zdraw_colors[(Ie)default]} -gt 0 ]] && color_default=1 || color_default=0
            (( has_default == color_default )) || exit 7
            # Changing an ordinary copy must not change the module's array.
            snapshot+=(application_value)
            (( ! ${zdraw_features[(Ie)application_value]} )) || exit 8
            typeset -a dimensions=(sentinel)
            expected=2
            (( ${zdraw_features[(Ie)geometry]} )) && expected=1
            # A compiled query can fail at runtime without losing its feature.
            zdraw geometry dimensions
            (( $? == expected )) || exit 9
            [[ $dimensions == sentinel ]] || exit 10
            (( ${#zdraw_windows} == 0 )) || exit 11
            print -rl -- "${zdraw_features[@]}"
        '''
        without_term = self.run_shell(script)
        invalid_term = self.run_shell(script, terminal='zdraw-nonexistent-terminal')
        self.assertEqual(without_term, invalid_term)
        self.assertNotIn('\x1b', without_term)

    def test_read_is_silent_and_does_not_consume_input(self):
        self.assertEqual(self.run_shell('''
            zmodload zdraw || exit 1
            inspect() {
                local -a snapshot=("${zdraw_features[@]}")
                read -r line || return 2
                [[ $line == 'input stays data' ]] || return 3
            }
            inspect <<<'input stays data' || exit 4
            (( ${#zdraw_windows} == 0 )) || exit 5
        '''), '')

    def test_independent_module_namespace(self):
        # Use the matching build's stock module, never an installed module.
        with tempfile.TemporaryDirectory(prefix='features-namespaces-', dir=ROOT / '.build') as tmp:
            modules = Path(tmp) / 'modules'
            (modules / 'zsh').mkdir(parents=True)
            suffix = re.search(r'^DL_EXT\s*=\s*(\S+)',
                               (BUILD / 'Src/Makefile').read_text(), re.M).group(1)
            shutil.copy2(ROOT / f'.build/modules/zdraw.{suffix}', modules)
            shutil.copy2(BUILD / f'Src/Modules/curses.{suffix}', modules / 'zsh')
            for stock_first in (False, True):
                with self.subTest(stock_first=stock_first):
                    prefix = 'zmodload zsh/curses || exit 20\n' if stock_first else ''
                    self.assertEqual(self.run_shell(prefix + '''
                        zmodload zdraw || exit 1
                        zmodload -F -e zdraw +b:zdraw +p:zdraw_features \
                            +p:zdraw_colors +p:zdraw_attrs +p:zdraw_keycodes \
                            +p:zdraw_windows +p:ZDRAW_COLORS +p:ZDRAW_COLOR_PAIRS || exit 2
                        zmodload zsh/curses || exit 3
                        zmodload -F -e zsh/curses +b:zcurses +p:zcurses_colors || exit 4
                        zmodload -F -e zsh/curses +p:zdraw_features && exit 5
                        zmodload -F -e zdraw +b:zcurses && exit 6
                        typeset -A info
                        zdraw textinfo info hello 3 || exit 7
                        [[ $info[text] == hel && $info[remainder] == lo ]] || exit 8
                        zmodload -u zdraw || exit 9
                        (( ! ${+zdraw_features} && ! ${+zdraw_windows} &&
                           ! ${+ZDRAW_COLORS} && ${+zcurses_windows} )) || exit 10
                        zmodload -F -e zsh/curses +b:zcurses || exit 11
                        zmodload zdraw || exit 12
                        zmodload -u zsh/curses || exit 13
                        zdraw textinfo info hello 3 || exit 14
                        zmodload -u zdraw || exit 15
                    ''', modules), '')

    def test_readonly_and_feature_lifecycle(self):
        self.assertEqual(self.run_shell('''
            zmodload -F -e zdraw +p:zdraw_features && exit 1
            # Discovery can be loaded alone, without the builtin.
            zmodload -F zdraw p:zdraw_features || exit 2
            zmodload -F -e zdraw +b:zdraw && exit 3
            typeset -a snapshot=("${zdraw_features[@]}")
            ( zdraw_features=(replacement) ) 2>/dev/null && exit 4
            ( zdraw_features[1]=replacement ) 2>/dev/null && exit 5
            ( unset zdraw_features ) 2>/dev/null && exit 6
            [[ "${(j: :)snapshot}" == "${(j: :)zdraw_features}" ]] || exit 7
            zmodload -F zdraw -p:zdraw_features || exit 8
            zmodload -F -e zdraw +p:zdraw_features && exit 9
            zmodload -F -e zdraw p:zdraw_features || exit 10
            (( ! ${+zdraw_features} )) || exit 11
            zmodload -F zdraw +p:zdraw_features || exit 12
            [[ "${(j: :)snapshot}" == "${(j: :)zdraw_features}" ]] || exit 13
            zmodload -u zdraw || exit 14
            (( ! ${+zdraw_features} )) || exit 15
            zmodload zdraw || exit 16
            [[ "${(j: :)snapshot}" == "${(j: :)zdraw_features}" ]] || exit 17
        '''), '')

    def variant(self, directory, source, make_args=(), stock=False):
        # All source edits and builds are confined to a disposable copied tree.
        tree = Path(directory) / 'zsh'
        shutil.copytree(BUILD, tree, symlinks=True)
        module = 'curses' if stock else 'zdraw'
        (tree / f'Src/Modules/{module}.c').write_text(source)
        suffix = re.search(r'^DL_EXT\s*=\s*(\S+)',
                           (tree / 'Src/Makefile').read_text(), re.M).group(1)
        result = subprocess.run(
            [os.environ.get('ZDRAW_MAKE', 'make'), '-C', str(tree / 'Src/Modules'),
             f'{module}.{suffix}', *make_args], capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        modules = Path(directory) / 'modules'
        destination = modules / 'zsh' if stock else modules
        destination.mkdir(parents=True)
        shutil.copy2(tree / f'Src/Modules/{module}.{suffix}', destination)
        return modules

    def test_build_without_optional_features(self):
        source = (ROOT / 'Src/Modules/zdraw.c').read_text()
        # Undefine after headers so system headers cannot re-enable a feature.
        source = source.replace('#include <stdio.h>', '''#include <stdio.h>
#undef TIOCGWINSZ
#undef HAVE_RESIZE_TERM
#undef NCURSES_MOUSE_VERSION
#undef NCURSES_VERSION
#undef HAVE_USE_DEFAULT_COLORS
#undef HAVE_WBORDER_SET
#undef HAVE_INIT_EXTENDED_PAIR
#undef HAVE_MVWIN
#undef HAVE_WRESIZE
#undef HAVE_PNOUTREFRESH
#undef HAVE_WCHGAT
#undef HAVE_COPYWIN
#undef HAVE_WADDCHNSTR
#undef HAVE_DEF_PROG_MODE
#undef HAVE_WADD_WCHNSTR''', 1)
        with tempfile.TemporaryDirectory(prefix='features-disabled-', dir=ROOT / '.build') as tmp:
            modules = self.variant(tmp, source)
            self.assertEqual(self.run_shell('''
                zmodload zdraw || exit 1
                zmodload -F -e zdraw +p:zdraw_features || exit 2
                typeset -A resources
                zdraw resourceinfo resources || exit 20
                [[ $resources[windows] == 0 && $resources[prepared_rows] == 0 &&
                   $resources[prepared_byte_limit] == unknown ]] || exit 21
                (( ${#zdraw_features} == 15 + (${zdraw_features[(Ie)grapheme_boundaries]} > 0) + (${zdraw_features[(Ie)wide_cell_inspection]} > 0) + (${zdraw_features[(Ie)resize_events]} > 0) + (${zdraw_features[(Ie)wide_text]} > 0) + (${zdraw_features[(Ie)wide_events]} > 0) &&
                   ${zdraw_features[(Ie)textinfo]} &&
                   ${zdraw_features[(Ie)text_positions]} &&
                   ${zdraw_features[(Ie)text_wrapping]} &&
                   ${zdraw_features[(Ie)structured_events]} &&
                   ${zdraw_features[(Ie)custom_borders]} &&
                   ${zdraw_features[(Ie)colorinfo]} &&
                   ${zdraw_features[(Ie)cell_inspection]} &&
                   ${zdraw_features[(Ie)window_snapshots]} )) || exit 3
                (( ! ${zdraw_colors[(Ie)default]} )) || exit 4
                typeset -a dimensions=(sentinel)
                zdraw geometry dimensions
                (( $? == 2 )) || exit 5
                [[ $dimensions == sentinel ]] || exit 6
                (( ${#zdraw_windows} == 0 )) || exit 7
            ''', modules), '')

    def test_stock_module_discovery_fallback(self):
        with tempfile.TemporaryDirectory(prefix='features-stock-', dir=ROOT / '.build') as tmp:
            # Stock means the selected shell's own module, with its matching C
            # API, rather than the newer provenance snapshot in upstream/.
            modules = self.variant(tmp, (BUILD / 'Src/Modules/curses.c').read_text(), stock=True)
            self.assertEqual(self.run_shell('''
                zmodload zsh/curses || exit 1
                zmodload -F -e zsh/curses +p:zcurses_features
                (( $? == 1 )) || exit 2
                (( ! ${+zcurses_features} && ! ${+zdraw_features} )) || exit 3
                (( ${#zcurses_windows} == 0 )) || exit 4
                zmodload -F -e zsh/curses +b:zcurses || exit 5
                zmodload -e zdraw && exit 6
                exit 0
            ''', modules), '')


if __name__ == '__main__':
    unittest.main(verbosity=2)
