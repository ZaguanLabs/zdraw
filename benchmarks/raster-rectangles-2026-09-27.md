# Rectangle batch experiment — 2026-09-27

**Decision: retain as experimental.** Captured Cinder Relay inputs produce exact
material/depth readback and identical half-block terminal bytes with compact
rectangle submission. The measured captured-input pipeline is 19–24% shorter
in half-block mode on this workstation. This is not a whole-game speedup.

## Reproduction and provenance

```sh
export ZSH_BUILD_ROOT="$PWD/.build/sources/zsh-5.9.2"
make test
python3 benchmarks/raster-rectangles.py > .build/rectangles-benchmark.json
python3 benchmarks/raster-rectangles.py --trials 1 --repetitions 2 --modes ascii mono
```

The [raw results](raster-rectangles-2026-09-27.json) record every trial and phase,
output hashes, source hashes, platform and shell version. Environment:
AMD Ryzen 9 5950X; Linux-6.18.44-desktop-1.mga10-x86_64-with-glibc2.42;
zsh 5.9.2 (x86_64-pc-linux-gnu); GCC 15.2.0,
`-Wall -Wmissing-prototypes -O2`, ncurses, `C.UTF-8`, `xterm-256color`.
The main timing run completed before the full regression suite started.

The [fixture](fixtures/cinder-rectangles.json.gz) contains 102 captured frames:
64×28, 80×36 and 100×52 pixels; low/high detail (`ray_step=2/1`); twelve
pump-hall sweep samples at original frame indices 0, 60, …, 660, plus five
door/ramp/raised-floor poses from the application’s render tests. Actors and
weapon rendering remain enabled; AI is inactive. Each size/detail combination
contains 17 frames. Original triangle readback supplies captured materials and
depths. Numeric argument spellings are retained as strings.

The supplied Cinder Relay archive is dated 2026-09-26 and is not a Git checkout.
Capture reads its original sources without editing them, using the matching
zdraw-built shell/module. The captured triangle pairs are checked to match the
specified rectangle expansion before a fixture is written. Normal builds, tests
and benchmarks do not access that source tree. Source SHA-256 values:

| Source | SHA-256 |
| --- | --- |
| `levels/relay.zsh` | `682e9566cd99ff272084fdbb3d7e3326dde5dd95e50aa67de1176e23d660ca36` |
| `lib/engine.zsh` | `d49cddd5cf8f4352dfb5ed187e109e640f29a6b7f16c8bbc74bd4e10b479e3ee` |
| `lib/art.zsh` | `658a1fc8560c263fdaa21ef6d7c8966fadce5483d2db58d5166db6e0697bbf3e` |
| `lib/render.zsh` | `f40cf9d6bd0529cc3b8e466d4a72627928723f83af9fc34851886945c0547711` |
| `game.zsh` | `3015e510de13cfa38ee06f0c8bdf5be6e3ba9c8c41fd33e52fe86140a0090e56` |

Fixture SHA-256: `cd500b12e403731631303fbc5679067b15e05eff254033273533b85529b0e710`.

## Measured captured-input pipeline

Each trial warms one complete 17-frame sweep, then measures five sweeps (85
frames). Three trials alternate backend order. The tables show medians of the
per-trial mean milliseconds/frame, not frame-time percentiles. Both backends
split the same captured rectangle inputs and call a Zsh rectangle helper that
builds a submission array. The triangle helper preserves the game’s original
20-word expansion; the compact helper appends seven words. Shell slicing,
expansion, builtin dispatch, validation and native filling are included in
“submission”. No assertion of isolated C-kernel timing is made.

| Pixels | Detail | Rectangles/frame | Triangle total ms | Rectangle total ms | Reduction |
| --- | --- | --- | ---: | ---: | ---: |
| 64×28 | Low | 190–563 | 7.820 | 6.309 | 19.3% |
| 64×28 | High | 273–779 | 10.010 | 7.879 | 21.3% |
| 80×36 | Low | 212–607 | 8.562 | 6.813 | 20.4% |
| 80×36 | High | 335–920 | 11.330 | 9.048 | 20.1% |
| 100×52 | Low | 249–680 | 9.244 | 7.302 | 21.0% |
| 100×52 | High | 391–1049 | 12.945 | 9.887 | 23.6% |

At 100×52, high detail, the phase breakdown is:

| Phase | Triangles ms | Rectangles ms |
| --- | ---: | ---: |
| Fixture splitting | 1.898 | 1.885 |
| Zsh argument construction | 9.174 | 6.903 |
| Clear | 0.009 | 0.008 |
| Submission + native raster | 1.285 | 0.533 |
| Packing + retained drawing | 0.051 | 0.049 |
| Stage/present | 0.276 | 0.267 |
| Total, including loop/timing overhead | 12.945 | 9.887 |

Totals include fixture iteration/splitting and timing overhead. They exclude
ray traversal, projection, texture/sprite run discovery, simulation and HUD
construction. Native traversal still visits both triangles’ bounding boxes;
the gain comes from reducing shell words and parsing work. The drained PTY
measures output generation/transport, not emulator painting or physical latency.

## Correctness and stopping point

`make test` passed all 184 tests with `ZSH_BUILD_ROOT` selecting public Zsh 5.9.2
and runtime checks using the shell built from that tree. The Zsh module manual
build and original raster benchmark smoke test also passed.

All 102 captured frames match the explicit triangle expansion exactly in
materials and 17-digit depth readback on this build. Captured original materials
also match; captured depths are checked within 2e-6 absolute/relative tolerance
to avoid promising cross-platform floating-point identity. Randomized batches
and focused cases cover vertical depth gradients, crossings, exact repeats,
shared edges, clipping, extreme coordinates, degenerate rectangles and empty
batches. Invalid late records, depths, reversed bounds, material IDs, count and
work budgets are rejected before any pixel changes. The exact work limit is
accepted; one additional full-surface rectangle is rejected atomically.

PTY checks exercise existing half/ASCII/monochrome writers, optional/failing
builds, suspend rejection, resize and resource cleanup with rectangle-generated
pixels. No new presentation or input path was added. Main benchmark output
hashes agree across both backends and all half-block trials.
Separate ASCII and monochrome replays also produced matching terminal bytes at
all sizes/details; their hashes are included under `fallback_verification` in
the raw results. Their timings are excluded because that verification ran while
the regression suite was active.

This evidence supports the bounded submission experiment. It does not promote
rasterization to a supported graphics family, demonstrate superiority over
external native renderers, or expand the existing real-terminal/portability
matrix. Textures, RGB raster storage, alpha and further primitives remain out
of scope. See [the contract](../docs/raster-experiment.md#rectangle-batches).
