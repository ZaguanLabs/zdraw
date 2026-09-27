# RGB raster experiment — 2026-09-27

**Retain as experimental.** Independent RGB storage, affine vertex colors,
area-box resolve and exact half-block packing meet the bounded correctness
contract. The captured painting's terminal output is identical through its
v0.1.2 presenter and the RGB packing path. This establishes a presentation-stage
benefit, not a faster complete painting build or supported graphics API.

## Reproduce

```sh
export ZSH_BUILD_ROOT="$PWD/.build/sources/zsh-5.9.2"
make test
python3 benchmarks/raster-rgb.py --trials 3 > .build/rgb-benchmark.json
```

The [raw measurements](raster-rgb-2026-09-27.json) record every phase and trial,
platform, matching shell version, fixture/source hashes, and terminal-stream
hashes. Tests and benchmarks use the committed fixture, not an application
checkout or installed module. The optional capture command is documented in
[the benchmark guide](README.md#rgb-raster-and-finished-painting-presentation).

Environment: AMD Ryzen 9 5950X, Linux 6.18.44 Mageia 10, GCC 15.2.0 with
`-Wall -Wmissing-prototypes -O2`, ncurses, public Zsh 5.9.2, `C.UTF-8`.
A controlled direct-color terminfo (`tests/truecolor.terminfo`) reserves the
first eight RGB numbers exactly like xterm-direct. A PTY is drained continuously;
these times do not measure an emulator's painting or compositor latency.

## Capture and equivalence

A fresh Alpine Vigil scene build submitted 89,360 triangles at 1500×792, then
ran its existing Zsh lighting, smoke, reconstruction and pair quantization to
produce 750×396 final RGB pixels. These final pixels match its existing cache
pixel for pixel. The capture never reads that cache and writes no application
files. Its self-contained fixture contains:

- 6,854 final palette colors and all 297,000 pixel indices;
- three complete geometry tiles (375×132), at origins (750,0), (750,264), and
  (375,660), with 1,646, 3,154 and 2,577 triangles respectively;
- the original pigment palette, exact triangle argument spellings, source hashes,
  shell version and a canonical P6 content hash.

The canonical PPM header is `P6\n750 396\n255\n`; its SHA-256 is
`832845349458f36f5e3ad6d9705bac40b031fe37a19a52a8cec76fda6773d68a`.

The reference backend is the captured application presenter, including its
whole-frame `colorplan` check. Only its clock changes to fractional `SECONDS`,
so the optional datetime module is unnecessary. The driver selects full detail,
RGB, no information row, and a 750×198 terminal. The native backend preloads nine
250×132 RGB surfaces using flat-colored geometry runs, then preflights their
union and blits them into the same window. Both use exactly 15,718 session
pairs: 15,717 artwork pairs and one black background pair. Every trial starts a
fresh terminal session; backend order alternates.

The entire 2,662,997-byte terminal stream is identical, including lifecycle and
presentation. SHA-256:
`ea8666debc94dd7b4a72057bef8eb5024c4dbbbb453c45c8b1d277cf278f5b2d`.

## Measurements

Three trials per backend, no concurrent test/build workload. Medians in milliseconds:

| Phase | Captured Zsh presenter | Native RGB |
| --- | ---: | ---: |
| Setup/import (excluded from frame) | 465.919 | 4360.574 |
| Prepare or preflight/pack | 1952.250 | 66.326 |
| Draw/present or stage/present | 55.492 | 52.873 |
| Completed frame, excluding setup | 2007.742 | 118.868 |

Completed-frame time excluding setup falls by 94.1%. This applies only to the measured presentation stage.

Reference preparation includes shell pixel decoding, run construction, staging,
whole-frame exact preflight and native prepared-row creation. Native packing
includes aggregate preflight, per-surface budget checks, pair allocation, cell
compilation and retained-window writes. Reference presentation also draws its
prepared rows before stage/present; native presentation only stages/presents.
Their combined frame times therefore compare equivalent completed output.

Setup is intentionally reported separately. The reference loads palette/indices
and constructs its alternating material/depth array. Native setup expands the
finished frame back into triangle runs and parses/rasterizes them. Python's
fixture-to-input-file generation is outside both timings. **This import adapter
is slower overall for an already completed painting.** The useful native path
requires producing and retaining colors in native surfaces in the first place.
The measurements do not claim that the painting's approximately 42-second
uncached build disappears, or that the application has already been ported.

## Correctness and limits

Validation: all **190 tests passed** in 355.796 seconds with the matching public
Zsh 5.9.2 build. The module manual builds successfully. Zsh 5.8 was not rerun
locally for this experiment.

An independent Python barycentric reference checks randomized RGB interpolation,
coverage, depth and clipping. A rational-arithmetic area oracle checks equal-size,
integer and noninteger box resolves, including explicit half-channel ties.
The three captured geometry tiles also compare their original indexed path with
separately colored triangles at exactly doubled inverse depth: every material
matches and every stored depth doubles exactly. Color conversion occurs in the
fixture adapter; this check does not assert fidelity equivalence between
8-bit vertex colors and the application's fractional-depth lighting model.

PTY checks cover explicit truecolor opt-in, canonical pair reuse, odd-height
packing, reserved RGB rejection, narrow writers, monochrome terminals, window
resize, suspend/resume, cursor preservation and cleanup. Budget failure must
allocate no pairs and change no cells; injected library allocation failure may
retain its already allocated pair but must change no cells. Tests also exercise
batch work limits, aggregate-plan limits and owned-byte exhaustion.

The API remains bounded by 32 surfaces, 65,536 pixels per surface and 8 MiB of
owned raster storage. Aggregate exact preflight adds bounded transient scratch.
No alpha, textures, image-file ingestion, terminal image protocol, implicit
presentation, pair recycling or approximation policy is introduced. Box resolve
uses display-encoded RGB and clears output depth; it does not implement the
painting's artistic postprocessing. Terminal-emulator visual comparison and a
complete native application port remain outside this experiment's evidence.
