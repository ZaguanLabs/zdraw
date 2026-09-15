# Performance benchmarks

The [2026-09-12 profiling sweep](performance-2026-09-12.md) records native and
component improvements, exact-output comparisons, profiling evidence and
commands for reproducing the measurements. `native.py` and `components.py`
can alternate a baseline and the working module/libraries in the same run.

## Experimental raster and cell packing

`python3 benchmarks/raster.py --repetitions 5` compares the native R1/R2
experiment with the captured Zsh scanline renderer on the same moving camera
sweep. It reports clearing, batch submission/rasterization, packing, presentation
and total elapsed time separately. The fixed fixture and reference backend are
included here; no game repository or installed module is needed. See the
[experiment contract](../docs/raster-experiment.md) for limits and interpretation.
The [2026-09-14 results](raster-2026-09-14.md) include the measured phase timings,
exact-output checks and supplemental live application observations.

## Styled spans

Build with the public Zsh source setup in the [repository README](../README.md),
then run from the repository root:

```sh
python3 benchmarks/spans.py --trials 7 --frames 500
```

The driver uses `.build/zsh/Src/zsh` and `.build/modules` in a 24x80 PTY, with
`TERM=xterm-256color` and the C locale. It requires no installed curses module,
external application or additional Python package. It emits JSON measurements.

Each frame draws 20 rows, each containing eight styled segments of eight ASCII
characters. The legacy path uses `move`, then `attr`/`string` for each segment,
and restores the cursor/style after each row. The batch path uses one `spans`
call per row. Both paths use the same colors, text and retained window contents.
Cell-for-cell equivalence is also tested by the ordinary PTY suite.

Every trial uses a fresh shell/curses session, allocates colors and draws 25 warmup
frames before timing. Timings use Zsh's floating-point `SECONDS` parameter. The
backend order alternates between trials. One column per row changes on successive
frames, so the refresh workload is not an unchanged-screen benchmark.

- `draw` times shell calls and retained-window drawing without refresh.
- `refresh` also includes one `zdraw refresh` per frame and draining terminal
  output into the PTY driver. It does not measure a graphical terminal emulator's
  paint latency.
- Byte totals cover the whole session, including initialization, warmup refresh
  and cleanup; these are excluded from timing. The driver captures actual bytes,
  not an estimate from the text length.

A local run on Linux x86-64, AMD Ryzen 9 5950X, Zsh 5.9.2, GCC `-O2`, and wide
ncurses produced the following medians across seven trials of 500 frames:

| Workload | Legacy ms/frame | Spans ms/frame | Speedup | Bytes per session, either path |
| --- | ---: | ---: | ---: | ---: |
| Drawing only | 0.687 | 0.116 | 5.95x | 4,383 |
| Drawing and refresh | 0.747 | 0.169 | 4.43x | 85,221 |

Drawing-only trial ranges were 0.666–0.694 ms for the legacy path and
0.114–0.120 ms for spans. With refresh they were 0.707–0.760 ms and
0.164–0.197 ms respectively. Results depend on the machine and workload; these
numbers describe this fixed ASCII frame, not arbitrary applications or Unicode
text. Batching reduces shell/module calls. It retains curses' existing screen
diff and emitted the same number of terminal bytes in this experiment.

## Prepared-row reuse

Include the prepared backend with:

```sh
python3 benchmarks/spans.py --prepared --trials 7 --frames 500
```

This runs all three backends and preserves the original two-backend default.
The prepared backend constructs two immutable rows before timing, differing in
one character. Each timed frame reuses the appropriate row twenty times. It
produces the same alternating content as the legacy and ordinary-span paths,
including the same warmup result. Preparation cost and memory are outside the
timed interval: this measures repeated reuse, not continually creating new rows.

A local Linux run on 2026-09-09 using Zsh 5.9.2, GCC `-O2`, wide curses and the
same 24x80 ASCII fixture produced these seven-trial medians (500 frames each):

| Workload | Ordinary spans ms/frame | Prepared ms/frame | Speedup over spans | Bytes/session, all three paths |
| --- | ---: | ---: | ---: | ---: |
| Drawing only | 0.126 | 0.054 | 2.32x | 4,383 |
| Drawing and refresh | 0.170 | 0.107 | 1.59x | 85,221 |

The [recorded measurements](prepared-2026-09-09.json) include trial ranges and
the legacy path. These are workload-specific results, not terminal paint times
or promises for arbitrary Unicode and frequently changing content. The prepared
backend skips repeated style parsing/text decoding and allocates no new colors
while drawing; curses still performs the physical-screen diff. Future work
should measure preparation amortization and representative changing workloads
before adding broader drawing batches.


## Rectangle fills

Run the uniform-rectangle comparison with:

```sh
python3 benchmarks/fill.py --trials 7 --frames 500
```

Each frame replaces a 20x64 rectangle with a single styled ASCII tile, alternating
between `X` and `Y`. The backends use twenty ordinary span calls, twenty prepared
row draws, or one `fill`. All use the same style, content, warmup and explicit
refresh schedule. Both prepared rows and their color pair are created before
timing in every backend. Preparation and allocation cost is excluded.

A local Linux run using the matching Zsh 5.9.2 shell, GCC `-O2`, wide curses,
`TERM=xterm-256color` and `LC_ALL=C` gave these seven-trial medians (500 frames):

| Workload | Row spans ms/frame | Prepared rows ms/frame | Fill ms/frame | Fill speedup over spans / prepared | Bytes/session, all backends |
| --- | ---: | ---: | ---: | ---: | ---: |
| Drawing only | 0.101 | 0.052 | 0.021 | 4.87x / 2.54x | 362 |
| Drawing and refresh | 0.153 | 0.102 | 0.073 | 2.08x / 1.39x | 131,100 |

The [recorded results](fill-baseline.json) include ranges. This is a uniform-fill
workload suited to `fill`, not a replacement for multi-style text rows. It measures
shell and curses execution, not terminal paint time. Every backend emitted the
same number of terminal bytes in each scenario. Fill removes shell row loops and
repeated tile/style compilation while retaining curses' normal screen diff.

## Character canvas

Build with the documented public Zsh source release, then run:

```sh
python3 benchmarks/canvas.py --trials 3 --frames 10
```

The benchmark uses the matching staged shell/module in a controlled PTY, a
32-segment integer waveform, and 8×32/16×64 cell grids. ASCII and Braille use the
same occupancy masks. Each trial creates a fresh shell, builds the scene, warms
up three frames and times ten frames using Zsh's floating-point `SECONDS`.
Backend order alternates between trials. Set `ZDRAW_TEST_LOCALE` if `C.UTF-8` is
unavailable. The script requires Unix PTYs and `wait4`.

- `baseline`: scene, loaded functions and an initialized window, without raster
  storage; this supplies a whole-shell memory reference, not a useful draw time.
- `raster`: rebuild the occupancy grid only.
- `cached`: retain the grid and perform row encoding plus native drawing.
- `rebuild`: rasterize, encode and draw each frame.

Drawn frames alternate between two foreground colors. They do not call refresh,
so timings do not measure terminal output transport or emulator paint latency.
Scene construction is outside timing. Peak RSS covers the whole shell, including
setup, scene construction, caches and warmup. On Linux the driver reads `VmHWM`
from `/proc` while the completed child waits for acknowledgement. Other systems
fall back to `wait4`, which may include launch overhead. Neither counter measures
canvas allocations in isolation, and differences between fresh processes include
allocator/library variation. Logical pixels, occupied cells and write attempts
are reported separately.

A local Linux x86-64 run with the matching Zsh 5.9.2 build and wide curses gave
these medians (three trials, ten measured frames each):

| Cells | Profile | Raster ms/frame | Cached draw ms/frame | Rebuild + draw ms/frame | Rebuild peak RSS KiB |
| --- | --- | ---: | ---: | ---: | ---: |
| 8×32 | ASCII | 9.184 | 6.634 | 15.785 | 4932 |
| 8×32 | Braille | 9.348 | 7.464 | 16.634 | 4960 |
| 16×64 | ASCII | 16.800 | 26.319 | 42.875 | 5360 |
| 16×64 | Braille | 15.997 | 28.319 | 45.041 | 5428 |

Baseline whole-shell peak RSS was 4708–4848 KiB across these cases. The smaller
raster contained 79 occupied logical pixels in 39 cells; the larger had 155 pixels
in 83 cells. Both retained 32 source segments. Full trial ranges, platform details
and logical resource counters are in the
[recorded JSON](results/canvas-2026-09-10.json).

These measurements support small plots and caching unchanged geometry. They do
not establish animation performance for arbitrary shapes or large filled scenes.
Cached drawing still validates/encodes cells in Zsh; native prepared rows are an
available separate reuse path, but were not timed in this benchmark. Keep these
results as evidence for the later diagnostics/optimization milestone rather than
moving the rasterizer into C without an application workload that needs it.

## Component boundaries

Build using the [public-source setup](../docs/building.md#build-and-test) and its matching shell,
then run from any directory:

```sh
python3 benchmarks/components.py --trials 5 --frames 20 --label local > components.json
# A shorter, focused run:
python3 benchmarks/components.py --workloads canvas --trials 3 --frames 10
```

The harness uses fresh `-df` Zsh processes in drained 24×80 Unix PTYs,
`xterm-256color`, and `C.UTF-8` (`ZDRAW_TEST_LOCALE` overrides the locale). It
warms up two complete frames and measures twenty. Workload order reverses on
alternate trials. Each case has separate repeated/changing variants and small
8×32 / large 16×64 drawing rectangles. Setup and final inspection are excluded
from timings. All samples are retained in the JSON.

| Workload | Timed work; change applied in the changing variant |
| --- | --- |
| Chart | Render bars from 32/64 signed samples; update one sample and rebuild the series |
| Canvas | Encode a retained Braille raster and draw it; change the first segment endpoint and rerasterize 31/63 segments |
| Form | Draw 4/12 fields, including preflight outside the viewport; replace the first character through editing actions |
| Document | Reflow 4/12 retained paragraphs and draw a viewport; alternate wrap width 32/31 or 64/63 (word wrapping can retain identical visible lines) |
| Surfaces | Rebuild a two-window shared tree, fill surfaces, copy the base and overlay a 4×12 floating surface; alternate base text and overlay position |
| Spans | Draw 8/16 full rows through ordinary styled spans; alternate text between `0` and `1` |
| Prepared | Draw the same rows from two prepared handles constructed before timing; alternate the handle |

`work_ms` includes Zsh functions, argument construction and called native drawing
operations. `stage_ms` and `present_ms` separately measure shell calls to those
operations, including dispatch/timing overhead. Tiny intervals near that overhead
are not C-only timings. `output_bytes` counts the **entire session**, including
setup, warmup and cleanup. The driver drains a PTY without an emulator; it cannot
measure paint latency or claim an interactive frame rate. Whole-shell Linux
`VmHWM` is sampled after snapshot serialization, so memory includes all loaded
companions, setup, inspection and reporting. It is `null` outside Linux `/proc`.

The baseline used `67b38c7`; the after run adds passive resource accounting and
removes the second raster-validation pass from canvas drawing. Both cohorts used
the same harness and system (AMD Ryzen 9 5950X, Linux x86-64, public Zsh 5.9.2,
wide ncurses, GCC with `-Wall -Wmissing-prototypes -O2`), with five fresh processes per case and twenty
measured frames per process. Cohorts ran sequentially, so small unrelated
changes are noise, not attributed improvements. The [before JSON](results/components-before-2026-09-10.json)
and [after JSON](results/components-after-2026-09-10.json) contain raw samples,
retained-cell SHA-256 hashes, output counts and (after) passive resource records.
To repeat the baseline, use a separate checkout at `67b38c7`, copy
`components.py` and `components.zsh` there from this milestone, then build it
using the same public Zsh source release and run the command above.

Large-rectangle work medians, in milliseconds per frame:

| Workload | Repeated before → after | Changing before → after |
| --- | ---: | ---: |
| Chart | 4.899 → 4.975 | 6.620 → 6.743 |
| Canvas | 28.733 → 17.613 | 56.139 → 45.265 |
| Form | 13.452 → 13.841 | 14.350 → 14.529 |
| Document | 8.117 → 8.075 | 7.875 → 8.236 |
| Surfaces | 0.050 → 0.052 | 0.054 → 0.053 |
| Spans | 0.109 → 0.112 | 0.112 → 0.114 |
| Prepared | 0.063 → 0.065 | 0.065 → 0.067 |

Removing duplicate cell validation reduced repeated large-canvas work by **38.7%**
and changing-canvas work by **19.4%**. Every public operation still validates all
cells; no cross-call cache or invalidation rule was added. All 28 final snapshot
hashes and session output lengths match the baseline, and repeated canvas drawing
is tested to retain the same native resource counts. Snapshot hashing includes
cell text, style, pair IDs and cursor metadata; matching byte *lengths* alone do
not prove terminal-byte identity.

The prepared workload reports two live rows, two successful preparations and
176/352 successful draws for the small/large cases (including two warmup frames).
It uses 2,234/4,282 accounted bytes in this build. Release and session reset are
covered separately by lifecycle tests. Companion models remain caller-owned;
see the [diagnostics guide](../docs/diagnostics.md) for their budgets and the
scope of all native counters.

**Decision:** defer generic native batching. Existing row/fill/copy operations
already make native composition small in these cases; avoiding dispatch alone
cannot explain or remove the multi-millisecond companion costs. The prepared
comparison supports existing explicit row reuse for stable content, without
introducing a new batch contract. Canvas remains the clearest candidate for a
separate acceleration proposal when larger or frequently changing scenes are
needed. That proposal should distinguish raster compilation from row encoding,
measure filled/erased and Unicode-fallback scenes, and retain current bounds and
failure behavior. This milestone does not add a native canvas API or approve a
batch API without its own validation, partial-failure and budget specification.
