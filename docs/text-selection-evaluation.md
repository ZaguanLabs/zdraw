# Text-selection experiment — 2026-09-17

This records evidence for the bounded experiment, not supported promotion or
a claim of superiority over pane-aware selection implementations.

## Implementation and automated evidence

The passive Zsh companion accepts canonical copy text, source-byte row mappings,
styles, explicit content geometry, a text policy and a revision. Event ownership,
source ranges, extraction and styled spans share the same retained mapping.
It introduces no terminal protocol, background reader or clipboard transport.
The only C change fixes the inherited `mouse delay` integer check: it compared
the parse-end pointer itself with NULL, rejecting valid numbers. A PTY fixture
failed on `delay 0` before the fix and passes afterward, including invalid and
overflowing argument rejection. Recorded upstream sources are unchanged.

The test fixtures cover:

- Forward/reverse source ranges, all edges/corners, outside press, padding,
  empty panes, literal spaces, blank rows, soft wraps and real line feeds.
- UTF-8 byte boundaries, CJK, combining marks, whole ZWJ graphemes and style
  fragments crossing a grapheme; row mappings that split units are rejected.
- Revision replacement, cancellation, draining old gestures, explicit reset,
  consumed-event routing, scrolling, append, modal ownership and resizing.
- Native retained-cell confinement and exact style restoration in color/mono.
- Real SGR mouse bytes through a PTY running the two-pane example, outside
  releases, disabled mouse, copying after narrow resize and terminal cleanup.

The complete `make test` suite contains 174 tests, including optional native
build variants and the five new test methods. The matching source/build used
for this run is Zsh 5.9.2 with ncursesw 6.5.20250802. The companion uses Zsh 5.8
syntax and APIs, but a separate Zsh 5.8 build was not run for this experiment.
The subsequent [Zsh 5.8 verification](portability/zsh-5.8.md) covers the full
native module and companion suite after the compatibility fixes.

## Real Kitty observation

The example was exercised through the desktop pointer/key tools and its pixels
were visually inspected; these are real Kitty windows, not a simulated terminal.
This is agent-operated inspection, not a claim of an independent human study.

Configuration: system Kitty **0.44.0**, X11, `--config NONE`, `font_size=12`,
default font selection and mouse bindings, the matching Zsh 5.9.2 shell, and the
shared `unicode-17.0.0-egc-wcwidth-sum-attach-zero` policy. The window manager
overrode the requested initial size; captured content sizes are 1267×550 pixels
for the color comparison and 420×340 for narrow monochrome. An 800×480 resize
and return to 420×340 were also inspected. No multiplexor was used.

Representative input is the standalone example's wrapped paragraph, blank
lines, indented shell code, CJK, combining accent and ZWJ sequence. The compared
gesture starts inside the first prose row and ends in the sidebar alongside
the code rows.

| Observation | Result |
| --- | --- |
| Ordinary drag, release in sidebar, then `c` | Highlight stops at text boundaries; escaped copied text contains prose and code only; sidebar click count remains zero. |
| Same gesture with Kitty Shift-drag | Kitty's highlight includes borders, padding, `NOT TEXT` and `Clicks:0`. Its selection remains independent of the application's copy buffer. |
| Narrow monochrome drag into sidebar | Selection remains distinguishable; borders and neighboring text remain unchanged; explicit copy still works. |
| Escape and redraw | Base colors/emphasis return. |
| Resize wider and back to narrow | Selection clears, rows reflow, and frames repaint cleanly after the example's redraw correction. |
| CJK/combining/ZWJ sample | Text remains within the native cell layout; this does not establish pixel-level agreement for all fonts and emoji shaping. |

Screenshots:

- [Application selection and extracted text](text-selection-captures/kitty-application.png)
- [Kitty Shift-selection on the same input](text-selection-captures/kitty-native.png)
- [Narrow monochrome application selection](text-selection-captures/kitty-mono-narrow.png)

The live resize check exposed stale terminal cells when the example only erased
the curses buffer after Kitty reflowed its old screen. Complete layout rebuilds
now use the existing `clear stdscr redraw` operation. Motion updates retain
incremental drawing. This is why passing PTYs alone was insufficient.

One locally built Kitty 0.48.2 launcher crashed before opening the test window;
it was not used for the comparison. No result is claimed for that build.

## Measurements

Command: `LC_ALL=C.UTF-8 .build/zsh/Src/zsh -df benchmarks/text-selection.zsh`.
Host CPU: AMD Ryzen 9 5950X. Five samples of 1,000 retained endpoint updates;
span samples average 20 complete 24-row viewport generations. Both documents
use the same 24×80 viewport and 70-character styled ASCII rows. Initialization
is timed separately. Other test work was running, so these are representative
local measurements, not isolated latency guarantees.

| Document | Source bytes | Initialization | Median event | Median spans, all 24 rows |
| --- | ---: | ---: | ---: | ---: |
| 100 lines | 7,100 | 57.0 ms | 53.0 µs | 18.6 ms |
| 10,000 lines | 710,000 | 405.0 ms | 53.0 µs | 19.0 ms |

These measure computation, not terminal presentation or input-to-pixel latency.
Drag updates do not rescan or rebuild the document. Span generation scales with
visible units, and can dominate event processing; the example writes only changed
rows and does not regenerate spans on idle timeouts. Source validation splits
logical lines once; it avoids repeatedly slicing the remaining long document.
Source extraction occurs only when the caller requests it.

## Remaining quality gate

The first deliverable demonstrates confined selection and extraction, but the
API remains provisional. Before supported promotion, compare a separately
versioned pane-aware selector on equivalent inputs, complete physical-pointer
edge/corner and lost-release checks across supported terminals, and verify a
separate minimum-version Zsh build. Automated edge/corner and cancellation
fixtures are not substituted for those outstanding observations. The current
work makes no promise of arbitrary Markdown source mapping, persistent
selection across reflow, off-screen selection, autoscroll or clipboard delivery.
