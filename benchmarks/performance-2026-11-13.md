# Style resolution and reliability sweep — 2026-11-13

This bounded follow-up measures and strengthens the existing text-and-cell
toolkit. The baseline is `947941fccccf4c35a9a87e4a5733c29f3dd17928`, including
the older-ncurses screen guard. No native API, terminal protocol, persistent
cache or new rendering feature is introduced.

## Measured change

Zsh function profiles attributed about 53% of the changing large-form workload
and 39% of the document reflow/draw workload to style resolution. The resolver
previously entered a helper function/options scope for every property, copied
its normalized assignment into a scratch association, then copied that value
into a precedence tier. Normalization now happens directly in the resolver and
writes to the appropriate tier. The property grammar, conditional precedence,
validation of inactive conditions and atomic assignment of the public result
remain intact. Theme values are read on every call, including mutable color
profiles; there is no cache to invalidate.

Five alternating trials, 30 measured frames per fresh process, two warmup frames,
24×80 PTY, `xterm-256color`, `C.UTF-8`, public Zsh 5.9.2, ncurses 6.5.20250802,
GCC `-O2`, Linux x86-64, AMD Ryzen 9 5950X. The baseline checkout's tree is also
available at PR head `c30214a`; its files are identical to the merge baseline.
Neither timing build uses instrumentation. These timings cover companion work
and native calls, excluding staging, presentation, setup and inspection.
The [raw measurements](results/performance-2026-11-13.json) retain samples,
output hashes, resource records and source/build fingerprints.

| Large workload | Baseline ms/frame | Updated ms/frame | Reduction |
| --- | ---: | ---: | ---: |
| Chart redraw | 4.244 | 3.515 | 17.2% |
| Chart update and redraw | 5.650 | 5.031 | 11.0% |
| Form redraw | 12.767 | 11.253 | 11.9% |
| Form edit and redraw | 13.744 | 12.038 | 12.4% |
| Document reflow/draw, fixed width | 7.891 | 6.913 | 12.4% |
| Document reflow/draw, changing width | 8.008 | 6.844 | 14.5% |

All 28 workload/size/change combinations retained identical cell snapshots,
terminal byte streams and resource records. Canvas, direct spans, prepared draws
and surface operations show no meaningful improvement; small changes in those
controls are measurement noise. Nothing here establishes terminal paint latency
or a portable frame-rate guarantee. Document reflow still recompiles caller-owned
data; skipping work based only on width would break its mutation contract.

## Reliability and compatibility

`tests/lifecycle-stress.zsh` varies geometry, shared window replacement, pad
resizing, prepared rows, rejected operations, explicit deletion, teardown with
live resources, suspend/resume, cached name reuse and active module unload.
Every cycle checks empty native resource counts after teardown. Stock-curses
mode draws through the retained foreign screen before and after each cycle;
on an unsafe library it instead requires the configured rejection path.

The optional `lifecycle.py` driver checks termios restoration and can run full
Valgrind leak checking. It snapshots the matching shell and module into a private
temporary directory, so a later build cannot truncate a loaded module. Build
before starting the snapshot. All copies remain under `.build/`.

Both standalone and stock coexistence passed 1,000 cycles with seed 20261113.
The normal suite includes a 40-cycle regression in each mode. The separate
Memcheck runs used full leak reporting, no suppressions, and a nonzero exit on
memory errors or definite/indirect leaks:

| Mode | Cycles | Memory errors | Definite/indirect lost bytes | Possibly lost bytes |
| --- | ---: | ---: | ---: | ---: |
| Standalone | 100 | 0 | 0 / 0 | 0 |
| Stock coexistence | 100 | 0 | 0 / 0 | 712 |
| Stock coexistence | 200 | 0 | 0 / 0 | 712 |

The possible-loss stacks point into libtinfo's `_nc_tiparm` allocations and
remain unchanged between these two cycle counts. This is not a whole-process
leak-free claim. Reachable allocations include the shell and retained foreign
screen, and their totals differ between runs; no RSS plateau is established.

The generated style comparison covers 2,000 cases with precedence, combined
states, mutable themes, invalid inactive properties, limits and unchanged output
on failure. Statuses and complete result associations match the baseline. It
also checks that caller-owned `REPLY` and `zdraw_color` remain unchanged.

The stock-curses differential corpus runs 600 seeded inherited operations,
including movement, styles, backgrounds, Unicode strings, clearing, scrolling
and borders. It compares statuses and cursor/current-cell records after every
operation, full cell readback every ten operations and at the end, plus terminal
bytes. All match in the recorded run. It deliberately uses ASCII for `char`:
stock's multibyte `char` decoding is an existing documented zdraw correctness
fix, so that operation is not an equivalence oracle. This does not establish
equivalence for every curses operation or platform.

## Prepared-row amortization

`native.py --reuse` compares ordinary spans against a prepare/draw/unprepare
group for a styled 64-column ASCII row. The row changes between groups. Every
preparation and disposal is inside the timer, with the same number of draws
and identical final cells in each backend. Color setup and shell startup remain
outside the timer; no refresh is timed. Results are per group, not per draw.
This measures reuse of one row, not caching continually changing documents.

Seven uncontended trials of 500 groups gave these medians:

| Draws per preparation | Spans µs/group | Prepared µs/group | Spans/prepared |
| --- | ---: | ---: | ---: |
| 1 | 14.39 | 16.48 | 0.87× |
| 2 | 17.43 | 18.95 | 0.92× |
| 4 | 24.38 | 21.51 | 1.13× |
| 8 | 37.80 | 28.86 | 1.31× |
| 16 | 65.55 | 44.06 | 1.49× |
| 32 | 114.66 | 72.75 | 1.58× |

Spans are cheaper at one and two draws; prepared rows show a clear benefit from
four draws onward in this fixture. Reuse count and content matter; this is not
a universal threshold. Both backends retain identical final cell and terminal
output hashes at each reuse count.

## Input and output pressure

`latency.py` injects 200 ordered keys per scenario and records completion after
each explicit presentation. It compares one key at a time, bursts of eight,
50 ms pauses in output draining, and those pauses combined with alternating
24×80/40×120 terminal sizes. Frames change the entire text area; explicit
suspend/resume is also exercised every 40 frames. Reports use a separate pipe
so they remain observable while PTY output draining is paused.

| Scenario | Median ms | p95 ms | p99 ms |
| --- | ---: | ---: | ---: |
| One key at a time | 0.184 | 0.246 | 0.266 |
| Eight queued keys | 0.799 | 1.341 | 1.491 |
| Eight keys, 50 ms output pause | 0.978 | 50.924 | 51.513 |
| Eight keys, pause and resize | 50.167 | 51.942 | 53.226 |

All 800 keys were observed in order and terminal modes were restored in all
four trials. These are local PTY injection-to-observed-completion measurements:
they include scheduling, report delivery and queued-key waiting, and exclude
emulator painting and network transport. The deliberate pause dominates the
upper tail. The results document blocking output behavior, not a promise that
an application can enforce a deadline while its terminal is blocked.

## Reproduction

The complete `make test` suite passed **193 tests without skips** using the
matching public Zsh 5.9.2 build. The changed Zsh files also passed syntax checks
with the matching Zsh 5.8 build; its headless style and diagnostics fixtures
passed as well. This is not a new full-suite Zsh 5.8 run or a BSD/macOS result.

First use the [public-source setup](../docs/building.md), keeping all build
products under `.build/`. Build both revisions with the same Zsh source release
and configuration. A module must never be loaded into a different-ABI shell.

```sh
export ZSH_BUILD_ROOT="$PWD/.build/sources/zsh-5.9.2"
export LC_ALL=C.UTF-8 ZDRAW_TEST_LOCALE=C.UTF-8
export LDFLAGS=-Wl,--as-needed
make build
git worktree add --detach .build/sweep-baseline 947941fccccf4c35a9a87e4a5733c29f3dd17928
(cd .build/sweep-baseline && make build)
python3 benchmarks/components.py --trials 5 --frames 30 \
  --baseline-root .build/sweep-baseline > .build/components-sweep.json
python3 benchmarks/style-differential.py --baseline-root .build/sweep-baseline
python3 benchmarks/curses-differential.py --operations 600 --seed 20261113
python3 benchmarks/lifecycle.py --cycles 1000
python3 benchmarks/lifecycle.py --cycles 1000 --mode stock
python3 benchmarks/lifecycle.py --cycles 100 \
  --valgrind-log .build/lifecycle-memcheck.log
python3 benchmarks/lifecycle.py --cycles 200 --mode stock \
  --valgrind-log .build/lifecycle-stock-memcheck.log
python3 benchmarks/native.py --reuse 1 2 4 8 16 32 --trials 7 --iterations 500
python3 benchmarks/latency.py --frames 200
make test ZSH_BIN="$PWD/.build/zsh/Src/zsh"
```

Run timing measurements without simultaneous builds, tests or instrumented
processes. Memory-check timings are not performance measurements. Memcheck fails
on memory errors and definite/indirect leaks, and reports possible losses and
reachable allocations separately; inspect those categories as well. Generated
stock-curses scripts and raw comparisons are retained under
`.build/curses-differential/`. The seed and operation count reproduce the corpus.
