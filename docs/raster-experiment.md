# Bounded raster experiment — R1/R2

Status: explicitly authorized experiment, 2026-09-14. The maintainer approved
native screen-space depth-tested triangles and half-block packing after reviewing
the z3dfx roadblocks and performance assessment. This API is experimental and
may change or be removed. It is not a supported graphics feature family.
The [scope exception](scope.md) does not authorize image protocols, textures,
scaled text, camera math, scene graphs, game state, input changes or scheduling.

## Contract

All operations are under `zdraw raster`. Merely loading the module allocates no
surfaces. Except `blit`, these operations work headlessly without `zdraw init`,
terminal discovery, output or input reads. A suspended terminal session rejects
raster operations until resumed, consistent with other drawing commands.

```text
zdraw raster create NAME WIDTH HEIGHT COLOR ASCII [COLOR ASCII ...]
zdraw raster clear NAME MATERIAL [LOWER_MATERIAL]
zdraw raster resize NAME WIDTH HEIGHT
zdraw raster triangles NAME [X0 Y0 Q0 X1 Y1 Q1 X2 Y2 Q2 MATERIAL ...]
zdraw raster read NAME ARRAY
zdraw raster info NAME ASSOCIATION
zdraw raster blit NAME WINDOW ROW COLUMN half|ascii|mono
zdraw raster free NAME
```

Names are identifiers of at most 64 bytes, without subscripts; they occupy a
separate namespace from windows and prepared rows. Duplicate creation fails.
Palette material IDs are zero-based, in creation order. Each palette entry
contains a decimal terminal color index in 0..255 and one printable ASCII
character. Color support is checked only when drawing in a color mode, so the
same headless surface can be inspected or drawn in monochrome on any terminal.
Palette entries are immutable: create a new surface to change them.

Creation and resize initialize every pixel to material 0, inverse depth 0.
Resize discards old pixels; a failed resize preserves them. Clear fills the
surface with the supplied material and resets depth to zero. With a second
material, rows starting at `floor(HEIGHT/2)` use that material. This supports
the reference scene's two-part background without submitting background pixels.
No operation implicitly resizes a window or adjusts geometry to terminal size.

`triangles` takes a flat batch of ten shell words per triangle. The caller
performs near-plane clipping and supplies screen x/y and **inverse depth**
`Q = 1/z`. The native operation accepts both vertex windings, clips its pixel
traversal to the surface, and ignores degenerate triangles. It does not cull
backfaces or perform perspective transforms. Batches accumulate until clear.

Numeric inputs are literal decimal numbers, optionally signed and with a decimal
point/exponent. They never evaluate shell arithmetic. Mantissas/exponents are
parsed independently of `LC_NUMERIC`; hexadecimal, whitespace, NaN, infinity,
trailing bytes and expressions are rejected. Words are limited to 64 bytes and
explicit exponent magnitudes to 300. Coordinates must be within ±32768;
inverse depth must be positive and at most 1,000,000. All triangles, material
indexes and the aggregate traversal budget are validated **before any pixel is
changed**. An empty batch succeeds without changing pixels.

Pixels are sampled at `(x+0.5, y+0.5)`. As in the Zsh reference, coverage includes
all three barycentric weights >= -0.000001. Triangles with absolute signed
double area <= 0.000001 do not draw. This is an inclusive-edge convention,
not a top-left ownership rule; adjacent triangles may both cover a shared edge.
Inverse depth is interpolated affinely with native double arithmetic, using
increments across each row and recomputing the row start. A fragment replaces
the stored pixel only when its computed inverse depth is strictly greater.
Equal computed depths keep the earlier pixel. Mathematically equal depths
computed from different geometry can differ by floating-point rounding.
This is deterministic for a given build/input, not a cross-platform bitwise
floating-point promise. The original Zsh reference rounds locals to eight
decimal places; comparisons therefore require exact materials and allow
2e-6 absolute/relative depth error for that reference.

`read` replaces an ordinary writable indexed array with row-major alternating
material/depth values (two elements per pixel). It is a diagnostic path, outside
the fast frame loop. Depth is formatted to 17 significant digits using the
process's numeric formatting locale. `info` replaces an ordinary writable
association with dimensions, palette size, owned bytes, total bytes and limits.
Its format tag is `zdraw-raster-experiment-1`. Subscript, special, read-only and
wrong-type destinations are rejected before assignment.

## Presentation and fallbacks

`blit` packs the surface into `WIDTH × ceil(HEIGHT/2)` cells in an existing
window or pad. The complete rectangle must fit. Each pair of vertical pixels
maps to an upper-half block, with the upper color as foreground and lower color
as background. Equal material IDs use a space with the same background. The
missing lower pixel of an odd-height surface uses material 0.

The caller explicitly selects one mode:

- `half`: requires wide-cell support, `MULTIBYTE`, a locale where U+2580 has
  width one, and usable palette colors.
- `ascii`: uses the upper pixel's palette character/color with material 0's
  background; the lower pixel is discarded.
- `mono`: uses the upper pixel's palette character and pair 0, without allocating
  color pairs; the lower pixel is discarded.

These fallbacks change fidelity and are not equivalent to two-color half blocks.
No terminal mode is negotiated. Status 2 means unavailable drawing support,
unrepresentable half blocks, or unavailable palette colors. Status 1 means
invalid arguments/resources, bounds, exhausted color pairs or a drawing failure.
Status 0 means success. An application can explicitly retry in `ascii` or `mono`.

Packing validates and compiles the entire rectangle before writing window cells.
It preserves the target's cursor, background and drawing attributes through the
existing row writer. A curses write failure may leave partially updated retained
cells, as with other drawing operations; it never presents them implicitly.
Use `stage` then `present`, or `viewport` then `present` for a pad. Freeing or
clearing the raster does not change previously drawn cells or snapshots.

Color pairs use the existing immutable session cache. Allocate only combinations
actually encountered; at most `palette_size²` combinations per surface, shared
through the normal cache where color strings match. Once allocated, pair IDs
remain valid until session end, including after surface deletion. A later pair
allocation failure can leave earlier pairs allocated, but leaves the target
window unchanged. Pair pressure is visible through `colorinfo` and
`resourceinfo`. Repeated frames reuse the surface's pair lookup table.

## Bounds and ownership

| Resource | Bound |
| --- | --- |
| Surface dimensions | 1..512 each, at most 65,536 pixels |
| Live surfaces | 32 |
| Palette | 1..32 entries |
| Triangles per call | 4,096 |
| Sum of clipped bounding-box pixel visits per batch | 16,777,216 |
| Owned raster storage across all surfaces | 8 MiB |

Owned storage includes structures, names, palette/pair tables and pixel/depth
buffers. Resize also checks old plus new buffers against that limit before
allocating. Per-call temporary storage is separately bounded by the triangle
limit (one parsed record per triangle), cell count (one packed rectangle and a
1,024-entry cell cache), or readback limit (two formatted values per pixel).
Shell argument/parameter memory and curses-owned cells/color pairs are additional.
`resourceinfo` adds `raster_surfaces`, `raster_bytes`, and `raster_byte_limit`.

`free`, `zdraw end` and module unload release native surfaces. `end` also releases
headless surfaces when no terminal session was started. Headless resources can
survive `init`; no color pairs exist until their first color blit. Suspend does
not discard surfaces, and resume keeps the existing session ownership rules.

## Example

Run with the shell/module built using [the public-source instructions](building.md).
The following Zsh fragment leaves terminal presentation explicit:

```zsh
zmodload zdraw || return
zdraw raster create mesh 60 30 0 ' ' 1 '#' 6 '*' || return
zdraw init || { zdraw raster free mesh; return 1; }
{
  zdraw raster clear mesh 0 || return
  zdraw raster triangles mesh \
    2 2 0.2 58 4 0.8 18 28 0.4 1 \
    4 24 0.8 50 2 0.2 56 28 0.6 2 || return
  zdraw raster blit mesh stdscr 0 0 half ||
    zdraw raster blit mesh stdscr 0 0 mono || return
  zdraw stage stdscr && zdraw present
} always {
  zdraw end
}
```

## Evidence and stopping point

The [2026-09-14 results](../benchmarks/raster-2026-09-14.md) record correctness,
phase timings, storage, and supplemental live xterm observations.

Run `make test` with `ZSH_BUILD_ROOT` set. Raster tests include independent direct
barycentric comparisons, deterministic randomized triangles, crossing depths,
both windings, shared edges, degenerate/offscreen geometry, large coordinates,
captured near-clipped scene geometry, malformed batches, work/memory limits,
readback targets, headless end/unload, actual curses cell inspection, odd heights,
resize, retained cells, and monochrome/narrow/unavailable/failing-color builds.

The compressed JSON in `benchmarks/fixtures/raster-sweep.json.gz` contains the
same 12-frame camera sweep at 64×32, 96×48 and 100×60, captured on 2026-09-14
with Zsh 5.9.2 from z3dfx's 168-source-triangle scene. It includes projected,
near-clipped triangles and the original Zsh renderer's expected pixels/depths.
It is a fixed input fixture, not an application dependency. The captured Zsh
scanline function in `benchmarks/raster-reference.zsh` is the comparison backend.
No normal build or test reads another repository.

```sh
python3 benchmarks/raster.py --repetitions 5 > .build/raster-benchmark.json
python3 benchmarks/raster.py --backends native --modes ascii mono
```

The benchmark separates shell batch splitting, clearing, submission/raster,
packing/retained drawing, and stage/present. It excludes scene construction,
camera transforms and clipping because the geometry is captured. It warms one
sweep, measures subsequent moving sweeps, and reports every phase and total
elapsed time. Its drained PTY includes native output work but does not measure
a graphical terminal emulator's paint latency. Do not derive a promised game
frame rate from it.

The experiment stops at correctness and performance evidence for this interface.
Promotion to a supported feature still requires the scope quality bar, including
comparison with the strongest relevant external alternatives, real-terminal
visual/interaction evaluation, portability evidence and an explicit decision.

### Support review, 2026-09-15

Retain R1/R2 as experimental. The recorded comparison demonstrates a substantial
improvement over the captured Zsh renderer, with matching output and bounded
storage/work. It does not yet satisfy the stronger comparison and portability
requirements for a permanent supported API. Existing xterm observations are
limited evidence; the interrupted R3 terminal run adds no compatibility evidence.
No image protocols, scaled text or further graphics features are authorized by
this review. Future promotion should use self-contained zdraw fixtures and
examples; external application changes are outside this task.
