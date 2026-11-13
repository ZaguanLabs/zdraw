#!/usr/bin/env python3
"""Compare inherited drawing operations with the matching stock zsh/curses."""
import argparse
import json
from pathlib import Path
import random
import shlex
import shutil
import tempfile

from native import ROOT, trial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--operations', type=int, default=600)
    parser.add_argument('--seed', type=int, default=20261113)
    args = parser.parse_args()
    if not 1 <= args.operations <= 10000:
        parser.error('operations must be 1..10000')
    rng = random.Random(args.seed)
    captures = ROOT / '.build/curses-differential'
    captures.mkdir(parents=True, exist_ok=True)
    lines = ['emulate -R zsh', 'setopt nounset', 'module_path=("$1")',
             'typeset cmd=$2 report_fd=$4 rc key', 'typeset -a cell position saved',
             'typeset -i y x operation',
             'if [[ $cmd == zcurses ]]; then zmodload zsh/curses; else zmodload zdraw; fi',
             '(( $? == 0 )) || exit 1', '"$cmd" init || exit 1',
             '"$cmd" addwin sample 6 16 1 1 || exit 1',
             'print -r -u "$report_fd" -- 0',
             '''record() {
               print -rn -u "$report_fd" -- "$operation"$'\0'"$rc"$'\0'
               "$cmd" position sample position || exit 2
               "$cmd" querychar sample cell || exit 2
               for key in "${position[@]}" "${cell[@]}"; do
                 print -rn -u "$report_fd" -- "$key"$'\0'
               done
               print -r -u "$report_fd" -- ''
             }
             snapshot() {
               "$cmd" position sample saved || exit 2
               for (( y=0; y<6; y++ )); do
                 for (( x=0; x<16; x++ )); do
                   "$cmd" move sample "$y" "$x" || exit 2
                   "$cmd" querychar sample cell || exit 2
                   for key in "${cell[@]}"; do print -rn -u "$report_fd" -- "$key"$'\0'; done
                 done
               done
               "$cmd" move sample "$saved[1]" "$saved[2]" || exit 2
             }'''.replace('\0', '\\0')]
    for i in range(args.operations):
        actions = [('move', 'sample', str(rng.randrange(-1, 7)), str(rng.randrange(-1, 17))),
                   ('attr', 'sample', rng.choice(('bold', '-bold', 'underline', '-underline',
                                                 'reverse', '-reverse', 'red/black', 'green/black', 'default/default'))),
                   # Stock's multibyte char decoding is a documented zdraw fix,
                   # not an equivalence oracle (docs/native-api.md).
                   ('char', 'sample', rng.choice(('X', ' ', 'Q', '!'))),
                   ('string', 'sample', rng.choice(('hello', 'Z9', '界x', 'e\u0301!', ''))),
                   ('clear', 'sample', rng.choice(('eol', 'bot', 'redraw'))),
                   ('bg', 'sample', rng.choice(('@.', '@ ', 'blue/black'))),
                   ('scroll', 'sample', rng.choice(('on', 'off', '1', '-1'))),
                   ('border', 'sample')]
        action = rng.choice(actions)
        lines += [f'operation={i}', '"$cmd" ' + ' '.join(shlex.quote(v) for v in action) + ' 2>/dev/null',
                  'rc=$?', 'record']
        if i % 10 == 0:
            lines += ['snapshot', '"$cmd" refresh sample || exit 2']
    lines += ['snapshot', '"$cmd" delwin sample || exit 2', '"$cmd" end || exit 2']
    with tempfile.TemporaryDirectory(prefix='curses-differential-', dir=ROOT / '.build') as tmp:
        tmp = Path(tmp)
        modules = tmp / 'modules'
        shutil.copytree(ROOT / '.build/modules', modules)
        suffix = next(modules.glob('zdraw.*')).suffix
        shutil.copy2(ROOT / f'.build/zsh/Src/Modules/curses{suffix}', modules / 'zsh')
        script = tmp / 'operations.zsh'
        script.write_text('\n'.join(lines) + '\n')
        shutil.copy2(script, captures / 'operations.zsh')
        samples = [trial(ROOT / '.build/zsh/Src/zsh', modules, backend, 1, fixture=script,
                         capture=captures / backend)
                   for backend in ('zcurses', 'zdraw')]
    if samples[0]['result_sha256'] != samples[1]['result_sha256']:
        raise RuntimeError(f'inherited operation statuses/cells differ: seed={args.seed}')
    print(json.dumps(dict(operations=args.operations, seed=args.seed,
                          identical_statuses_and_cells=True,
                          identical_terminal_bytes=samples[0]['output_sha256'] == samples[1]['output_sha256'],
                          samples=dict(zip(('zcurses', 'zdraw'), samples))), indent=2))


if __name__ == '__main__':
    main()
