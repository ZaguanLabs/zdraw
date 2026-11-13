# Changelog

## 0.1.4 — 2026-11-13

- Reduce companion style-resolution overhead while preserving conditional
  precedence, theme mutations and validation of inactive utilities. The measured
  large form/document workloads use 12–15% less work time, and charts 11–17%
  less, with identical cells, terminal bytes and resource counts. See the
  [performance and reliability report](benchmarks/performance-2026-11-13.md).
- Probe whether curses can delete one screen without freeing another screen's
  windows. On affected ncurses builds, reject `zdraw init` while another screen
  is retained, before allocating a screen or changing the other owner's windows.
  Fixed and backported libraries retain screen coexistence; use a separate
  process or a fixed ncurses build on affected systems.
- Add seeded lifecycle regressions, stock-curses and style comparisons, full
  leak-check tooling, prepared-row amortization measurements and controlled
  input/presentation latency probes. Memory investigations use private shell and
  module copies so later builds cannot overwrite a loaded test module.

Validation: 193 tests passed without skips with matching Zsh 5.9.2. The changed
Zsh files also passed syntax checks on Zsh 5.8, alongside its headless style and
diagnostics fixtures. The investigation includes 2,000 style comparisons,
600 stock-curses operations, 2,000 lifecycle cycles and 800 ordered input events.
Memcheck reported no errors or definite/indirect leaks; stock coexistence retained
712 bytes classified as possibly lost in libtinfo at both 100 and 200 cycles.

When upgrading an existing checkout, run `make clean` before rebuilding: the
configure integration patch has changed. Use the matching shell/module setup in
the [build guide](docs/building.md).

## 0.1.3 — 2026-09-27

- Add experimental RGB raster materials and affine per-vertex RGB interpolation,
  with inverse depth stored independently.
- Add bounded area-box resolve into a new RGB surface, exact half-block/ASCII
  packing, and non-allocating aggregate pair preflight. Presentation remains
  explicit; monochrome uses material fallback glyphs.
- Capture Alpine Vigil geometry and its finished painting for standalone tests
  and [presentation measurements](benchmarks/raster-rgb-2026-09-27.md). Import
  cost is reported separately; these measurements are not a whole-build speedup.
- Specify encoded RGB interpolation, exact box rounding, zero resolved depth,
  output capability failures, ownership, and the experiment's stopping point.

Validation: all 190 tests passed with matching Zsh 5.9.2; the module manual
builds successfully. All six painting benchmark streams are byte-identical.

## 0.1.2 — 2026-09-27

- Add experimental `colorplan` preflight for exact color-pair reuse, demand and
  drawing-path limits without allocating pairs or changing terminal state.
- Add experimental raster rectangle batches with material and top/bottom inverse
  depth, preserving the existing two-triangle coverage and depth behavior.
- Capture 102 Cinder Relay frames in a self-contained fixture. Compact batches
  reduce the measured captured-input half-block pipeline time by 19–24%, including
  Zsh argument construction; this is not a whole-game speedup. See the
  [benchmark results](benchmarks/raster-rectangles-2026-09-27.md).
- Document bounded APIs, resource limits and explicit stopping points. These
  additions remain provisional and do not reopen the broader graphics roadmap.

Validation: all 184 tests passed with the matching Zsh 5.9.2 build. The module
manual builds successfully; rectangle and triangle terminal output matches in
half-block, ASCII and monochrome modes. Zsh 5.8 remains covered by the separate
CI job; it was not rerun locally for this release.

## 0.1.1 — 2026-09-17

- Support native Zsh 5.8 builds: use build-patch context shared by both
  releases and an owned-array conversion independent of newer internal APIs.
- Add separate Zsh 5.8 and 5.9.2 CI jobs, including matching-shell syntax checks,
  with reproducible minimum-version build instructions.
- Fix outside-origin selection drags when motion repeats `PRESSED1`, including
  layout replacement during a held gesture.

Validation: 174 tests passed on each matching Zsh 5.8 and 5.9.2 build, with no
skips. See the [compatibility verification](docs/portability/zsh-5.8.md).

## 0.1.0 — 2026-09-17

First versioned release of the existing zdraw toolkit. Earlier revisions were
identified by Git commits.

- Independent Zsh module derived from `zsh/curses`, with styled cell drawing,
  Unicode text geometry, retained surfaces, structured input and terminal
  lifecycle operations.
- Optional Zsh companions for themes, layouts, panels, lists, tables, forms,
  documents and compact data displays.
- Experimental pane-confined text selection with source-text extraction,
  retained highlighting, explicit drag ownership and content invalidation.
  Includes a two-pane example, automated tests and recorded Kitty observations.
- Fixed validation of the native `mouse delay` argument, allowing applications
  to disable click aggregation for responsive dragging.

The text-selection API and the separately authorized raster experiment remain
provisional. Versioning does not change the [project scope](docs/scope.md) or
promote experimental interfaces to supported APIs.

Validation: 174 tests passed with the matching Zsh 5.9.2 build. The module still
requires a matching Zsh ABI; a release version does not make its binary portable
between unrelated shell builds.
