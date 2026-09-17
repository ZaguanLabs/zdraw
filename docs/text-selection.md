# Experimental pane-confined text selection

Status: bounded experiment approved by the maintainer on 2026-09-17 after
review of zcoder's text-selection request. Names and state are provisional.
This does not reopen the roadmap or designate a supported feature family.

The optional companion selects a reading-order range within an explicit content
rectangle. Both highlight spans and copied bytes come from caller-supplied text
and mappings. Terminal cells are never read back to reconstruct copied text.
No dependency on zcoder, clipboard program, additional runtime, or second input
reader is introduced. Zsh 5.8+ and the matching zdraw module are required.

## Try it

Build using the public-source setup in [README](../README.md), then run:

```sh
.build/zsh/Src/zsh -df examples/text-selection.zsh --mouse
.build/zsh/Src/zsh -df examples/text-selection.zsh --mouse --mono
```

Ordinary left drag selects; `c` retrieves the application selection and displays
its escaped text separately. Actual line feeds appear as `\n`; soft wraps do
not. Drag across the sidebar, border, and footer to check confinement. `Esc`
clears, `j/k` scroll, `a` simulates appended output, `m` toggles a modal input
owner, `d` disables/enables mouse selection, and `q` exits. `f` shows all logical
text as a keyboard fallback. Without `--mouse`, no mouse reporting is requested.
The example uses cell wrapping and retains indentation and literal spaces.

Kitty's Shift-drag remains terminal-owned and can select adjacent UI cells.
Its Ctrl+Shift+C copies Kitty's selection, not this component's highlight.
The example's `c` deliberately retrieves text without delivering it to a system
clipboard. See [Kitty's mouse bindings](https://sw.kovidgoyal.net/kitty/conf/#mouse.Start-selecting-text-even-when-grabbed).

## Caller-owned data and API

Source `lib/zdraw-text-selection.zsh`; loading is passive. Declare an ordinary
writable association `zdraw_text_selection` in the instance's calling scope.
Multiple instances use separate scopes and copies, following the existing
companions' dynamic-scope convention. Internal `_zts_*` names are reserved.

```zsh
typeset -A zdraw_text_selection zdraw_selection_event
typeset -a reply
typeset REPLY
zdraw-text-selection-init $'hello world\n  code' view-1 2 3 5 8 native \
  0 0 0 bold 'hello ' \
  1 0 6 '' 'world' \
  2 0 12 underline '  code'
```

`init TEXT REVISION Y X HEIGHT WIDTH POLICY [ROW COLUMN BYTE STYLE TEXT ...]`
replaces layout and clears selection atomically after validating geometry,
source text and mappings. `Y/X` are screen coordinates of the content rectangle;
`ROW/COLUMN` are relative to that rectangle, never to an outer window. Exclude
borders, titles, padding, gutters and decorations. Offset `BYTE` is into the
canonical copy text. All positions are zero-based; returned range ends are
exclusive UTF-8 byte offsets, never Zsh character indices.

Each record maps a literal source slice to a displayed row. Records are in
screen and source order. Adjacent style fragments on the same row repeat its
column origin and use contiguous source offsets; the compiler joins them before
segmenting text. The first scalar's style applies to a whole text unit. A row
has one contiguous selectable segment; its prefix/suffix may be decorations.
Only actual newlines may be omitted between mapped rows. This catches accidental
copying of hidden markup or unmapped labels between endpoints. Leading/trailing
unmapped source is allowed for a viewport. Empty records map real blank lines;
unmapped rows represent empty display area.

`TEXT` is the caller's **canonical copy text**, such as Markdown's displayed
text with markup and gutter labels already removed. It is not raw Markdown.
This milestone deliberately does not support arbitrary noncontiguous mappings,
tab expansion, folds inside a selected range, or unrelated source blocks joined
implicitly. Build the desired logical projection first. Keep meaningful spaces
and real newlines in that projection; never supply padding as text.

Policies are `native` (the existing positive-width scalar plus attached marks)
or `unicode-17.0.0-egc-wcwidth-sum-attach-zero`. The query-only `grapheme` mode is
not accepted. Validate capability before choosing the safe policy; unsupported
queries return native status 2. Drawing must use the same policy. Locale and
`MULTIBYTE` must remain fixed for the lifetime of a mapping. For the native mode,
check both native query and drawing capabilities before accepting non-ASCII.
See [the width policy](grapheme-width-policy.md): emoji are measured with summed
libc widths, not guaranteed two-cell terminal shaping. Reflow/region edits must
respect complete units. The existing document companion is not silently
upgraded by this experiment.

Limits: source at most 16 MiB, rectangle dimensions at most 4096 cells, and
screen extents at most 32767. Initialization validates source and builds only
the viewport's cell lookup and styled units. It is intended for layout/revision
changes, not motion events. Memory scales with source plus mapped viewport.
Initialization rescans logical lines for mapped row boundary checks; very long
source lines and large viewports can make compilation expensive.

## Events, ranges, and ownership

Pass the association returned by `zdraw event` as `zdraw_selection_event`, then
call `zdraw-text-selection-event REVISION`. Status 0 means the event was handled
successfully, including events that do not belong to this component. Inspect
`zdraw_text_selection[consumed]` **before dispatching to any other pane**.
Errors use status 1 with a diagnostic; native unsupported queries can use 2.

The component exposes these result fields (other keys are private):

| Field | Meaning |
| --- | --- |
| `active` | The component owns a held left-button drag. |
| `selected` | A range is available. |
| `start`, `end` | Normalized source byte range `[start,end)`. |
| `consumed` | The last passed event must not reach another pane handler. |
| `changed` | Selection appearance changed during the last event. |
| `valid` | The current mapping has not been invalidated. |

A plain left press over a text unit selects that whole unit and anchors a drag.
Modified presses and presses in padding/decorations do not start it. Dragging
includes complete anchor and endpoint units in either direction. Beyond the
left/right content edge, the endpoint is the row's first/last text boundary.
Above/below the rectangle, the row is clamped first. Unmapped rows resolve to
the previous mapped row's end, or the first mapped start when none precedes it.
No screen padding is selected or copied. A release anywhere delivered to the
application ends the drag; the selected range remains.

While active, all mouse events are consumed. Native motion events may have an
empty button list or repeat `PRESSED1`; neither reanchors a held gesture.
The application must request press/release events rather than click aggregation:

```zsh
# The application owns this terminal session and its mouse configuration.
zdraw mouse delay 0 motion
zdraw event stdscr zdraw_selection_event mouse norefresh
zdraw-text-selection-event "$revision" || return
if (( zdraw_text_selection[consumed] )); then
  # Redraw changed selection and skip other mouse handlers.
  :
fi
```

Discover `mouse`, `structured_events`, and `norefresh_events` capabilities before
using optional flags. The companion itself never changes global tracking, click
delay, input timeout, traps, or terminal ownership. The example opts in for its
own session, disables motion when another owner is active, omits `mouse` from
reads when disabled, and ends the session in an `always` cleanup block. Embedded
applications must keep their own prior mouse configuration and restore it when
releasing ownership; there is no mouse-configuration query in this API.

`zdraw-text-selection-clear` clears selection while retaining a usable mapping.
Escape does the same and is consumed only when there was a selection/drag.
`zdraw-text-selection-invalidate` also rejects further selection until `init`.
A changed revision passed to `event`, a resize, or a reported focus loss
invalidates automatically. Call invalidate explicitly for scrolling, reflow,
replacement, pane removal, locale changes, modal entry or ownership loss.
Never mutate private retained state. Reinitialize after changing geometry/text.

Cancellation during a held gesture drains subsequent mouse events through the
release; they cannot accidentally activate a neighboring control. Repeated
button states cannot reanchor a cancelled drag. If the release is lost outside
the terminal, the next press/release pair is drained too. The explicit
`zdraw-text-selection-reset` operation clears selection and this drain when the
application knows the old gesture has ended, retaining the mapping's validity
state. Do not reset while a button is still held. The application should cancel
on known ownership/focus loss; the component cannot detect undelivered events.
Background output scheduling remains entirely with the application.

`zdraw-text-selection-get` returns exact selected bytes in `REPLY`, or an empty
string if no valid selection exists. Call it directly: command substitution
would remove trailing newlines. Copy transport is application-owned.

## Rendering

`zdraw-text-selection-spans ROW [ATTRIBUTE]` returns style/text pairs in `reply`.
Draw them at that mapped row's origin using `zdraw spans`, adding `policy=NAME`
for the safe policy. It never draws decorations, fills padding, presents a
frame, or changes the drawing window's attributes. Callers retain the geometry
they supplied. Status 1 rejects an invalid row or highlight attribute.

Default `reverse` toggles existing reverse/standout and preserves other base
attributes and colors, so monochrome selection remains visible. Alternatively
add `underline`, `bold` or `standout`; callers must choose a treatment distinct
from their original styles. The native drawing call validates style syntax.
Original styles are retained; clear returns the original spans exactly (within
the selected policy's first-scalar style rule). Redraw affected rows after
clear or range changes. After replacing/removing a layout, the application
must repaint its former content rectangle as with any other removed component.
The example caches returned rows and only writes changed text rows.

## Verification and stopping point

`tests/test_text_selection.py` runs source-range/event-sequence fixtures, native
cell/style snapshots, and a PTY example with actual encoded mouse bytes. It
checks cross-pane routing, outside releases, disabled mouse, append, scrolling,
modal cancellation, narrow/tiny resize, monochrome, and terminal-mode cleanup.
Run the full suite with `ZSH_BUILD_ROOT=/path/to/zsh-source make test`.

`benchmarks/text-selection.zsh` measures retained motion updates for short/long
documents independently of initialization and span generation. See
[the experiment record](text-selection-evaluation.md) for measured results,
real Kitty screenshots and the remaining comparison work. PTYs establish
protocol and cell behavior, not correct physical pointer interaction or visible
glyph shaping.

Required before supported promotion: compare the same representative inputs
in a versioned/configured Kitty session and a pane-aware selection alternative;
record ordinary/narrow widths, edge/corner drags, lost release, resize, fallback,
visual restoration and responsiveness. No superiority claim is made here.
Autoscroll, off-screen selection, word/paragraph gestures, clipboard protocols,
selection-preserving remapping and application frameworks remain out of scope.
