#!/usr/bin/env python3
"""Compare seeded style statuses and complete outputs with an earlier checkout."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-root', type=Path, required=True)
    parser.add_argument('--cases', type=int, default=2000)
    parser.add_argument('--seed', type=int, default=20261113)
    args = parser.parse_args()
    if not 1 <= args.cases <= 10000:
        parser.error('cases must be 1..10000')
    rng = random.Random(args.seed)
    properties = ['bold', 'no-bold', 'underline', 'no-underline', 'reverse', 'no-reverse',
                  'fg=text', 'fg=accent', 'bg=surface', 'fg=003', 'fg=255', 'fg=default',
                  'border-fg=#ABCDEF', 'bg=#123456', 'px=00', 'px=16', 'py=2',
                  'border=rounded', 'border=none', 'align=left', 'align=center', 'align=right',
                  'bogus', 'fg=256', 'bg=', 'px=17', 'py=-1', 'px=evil=1',
                  'focus:fg=bogus', 'border=cloud', 'fg=$(false)', 'no-fg=003']
    conditions = ['', 'focus:', 'normal:', 'focus+selected:', 'invalid:',
                  'cursor:', 'heading:', 'inactive:', 'focus++selected:', 'hover:', ':']
    states = ['normal', 'focus', 'focus,selected', 'inactive', 'disabled,invalid',
              'focus,cursor', 'heading', '', 'focus,,selected', 'hover']
    lines = ['emulate -R zsh', 'source "$1/lib/zdraw-ui.zsh" || exit 1',
             'typeset -A zdraw_ui_style zdraw_ui_theme', 'typeset rc key REPLY=sentinel',
             'typeset -A zdraw_color=(sentinel unchanged)',
             '''dump() {
               print -rn -- "$rc"$'\0'
               for key in "${(@ok)zdraw_ui_style}"; do
                 print -rn -- "$key"$'\0'"$zdraw_ui_style[$key]"$'\0'
               done
               [[ $REPLY == sentinel && $zdraw_color[sentinel] == unchanged ]] || exit 3
             }'''.replace('\0', '\\0')]
    # Successful combinations, invalid inactive variants and mutable themes.
    for i in range(args.cases):
        limit = 22 if i % 2 == 0 else len(properties)
        valid_conditions = conditions[:8] if i % 2 == 0 else conditions
        state = rng.choice(states[:7] if i % 2 == 0 else states)
        utilities = [rng.choice(valid_conditions) + rng.choice(properties[:limit])
                     for _ in range(rng.randrange(0, 18))]
        if i % 100 == 0:
            utilities = ['bold'] * (128 + (i // 100) % 2)
        elif i % 100 == 1:
            utilities = ['fg=' + '0' * (125 + (i // 100) % 2)]
        lines += [f'zdraw-ui-theme dark {rng.choice(("16", "256", "mono"))} || exit 2',
                  'zdraw_ui_theme[accent]=' + shlex.quote(rng.choice(('003', '#ABCDEF', 'default', '256'))),
                  'zdraw_ui_style=(sentinel unchanged)',
                  'zdraw-ui-style ' + ' '.join(shlex.quote(v) for v in [state, *utilities]),
                  'rc=$?', 'dump']
    env = {**os.environ, 'LC_ALL': os.environ.get('ZDRAW_TEST_LOCALE', 'C.UTF-8')}
    with tempfile.TemporaryDirectory(prefix='style-differential-', dir=ROOT / '.build') as tmp:
        script = Path(tmp) / 'styles.zsh'
        script.write_text('\n'.join(lines) + '\n')
        outputs = []
        for root in (args.baseline_root, ROOT):
            result = subprocess.run([ROOT / '.build/zsh/Src/zsh', '-df', script, root.resolve()],
                                    env=env, capture_output=True, timeout=60, check=True)
            if result.stderr:
                raise RuntimeError(result.stderr.decode(errors='replace'))
            outputs.append(result.stdout)
        if outputs[0] != outputs[1]:
            raise RuntimeError('style status/output differs from baseline')
    print(json.dumps(dict(cases=args.cases, seed=args.seed, identical=True,
                          output_sha256=hashlib.sha256(outputs[0]).hexdigest()), indent=2))


if __name__ == '__main__':
    main()
