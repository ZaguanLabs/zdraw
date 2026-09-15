# Native API guide

The module command reference is [Doc/Zsh/mod_zdraw.yo](../Doc/Zsh/mod_zdraw.yo).
This guide supplies examples and explanations of the extended operations.
See [building](building.md) for setup and the [UI toolkit](ui-toolkit.md) for components.

The separately authorized `zdraw raster` interface is an
[experimental addition](raster-experiment.md), with its own bounded contract,
reference fixtures and benchmarks. It is not a supported graphics API.

## API

### Compiled feature discovery

After loading the module, use Zsh's standard module-feature check to discover
whether the read-only `zdraw_features` array is available:

```zsh
zmodload zdraw
if zmodload -F -e zdraw +p:zdraw_features; then
  if (( ${zdraw_features[(Ie)geometry]} )); then
    print -r -- 'Native terminal-size queries are compiled in.'
  else
    print -r -- 'Native terminal-size queries are not compiled in.'
  fi
else
  print -r -- 'Compiled feature information is unavailable.'
fi
```

This example works without a controlling terminal or `zdraw init`. When this
parameter is unavailable, the feature check returns 1 silently without invoking
an unsupported `zdraw` command. A missing or disabled parameter means support is
unknown, so the application chooses its fallback policy.

| Feature name | Compiled support |
| --- | --- |
| `text_policy` | Headless width-policy discovery |
| `grapheme_safe_text` | Complete opt-in grapheme-safe queries and native drawing |
| `geometry` | Terminal-size queries through `TIOCGWINSZ` |
| `resize` | Curses resizing through `resize_term` |
| `mouse` | Ncurses mouse input and configuration |
| `default_colors` | The `default` color name through `use_default_colors` |
| `custom_borders` | Eight-character borders, including printable ASCII |
| `wide_borders` | Unicode borders through `setcchar` and `wborder_set` |
| `window_snapshots` | Bounded capture of retained window cells into an association |
| `cell_occupancy` | Optional conservative occupancy metadata in snapshots |
| `cell_inspection` | Structured readback of the current retained cell |
| `wide_cell_inspection` | Complete complex-character text, including stored combining marks |
| `colorinfo` | Runtime color capabilities and allocation information |
| `resource_info` | Passive resource counts, budgets and prepared-row reuse |
| `truecolor` | Optional ncurses extended-color APIs and terminfo queries for RGB |
| `structured_events` | Associative input records using the curses decoder |
| `streaming_paste` | Opt-in bracketed paste through curses, in bounded byte chunks |
| `suspend_resume` | Foreground terminal handoff while retaining drawing resources |
| `event_poll` | Poll one event without changing the window's configured timeout |
| `input_info` | Input descriptor, polling guidance and session input state |
| `capability_evidence` | Passive capability records with evidence sources and explicit overrides |
| `capability_queries` | Opt-in bounded mode queries through the existing event owner |
| `synchronized_output` | Evidence-gated mode-2026 markers around explicit `present` calls |
| `focus_events` | Opt-in terminal focus reports with explicit ownership |
| `keyboard_events` | Negotiated kitty keyboard subset with key actions, modifiers and text |
| `input_delay` | Explicit ncurses escape-decoder delay with end/unload restoration |
| `norefresh_events` | Opt-in event input without refreshing drawing windows (ncurses) |
| `wide_events` | Locale-based wide-character input; otherwise events contain raw bytes |
| `resize_events` | Terminal size queries or curses resize key notifications |
| `prepared_rows` | Immutable session-scoped styled rows, with clipping and inspection |
| `region_fill` | Styled rectangle fills using single-column tiles |
| `region_copy` | Bounded opaque copies of retained rectangles |
| `transparent_copy` | Bounded copies with plain spaces as holes and styled blanks opaque |
| `region_restyle` | Replace rectangle styles while retaining character data |
| `offscreen_pads` | Bounded offscreen surfaces and viewport staging |
| `pad_resize` | Resize public pads while retaining overlap and drawing state |
| `window_movement` | Move independent ordinary windows |
| `window_resize` | Resize and optionally reposition independent ordinary windows |
| `window_trees` | Bounded geometry reconstruction for shared window trees |
| `staged_refresh` | Stage ordinary windows and explicitly present a composed frame |
| `styled_spans` | Single-row styled text batching |
| `wide_spans` | Wide characters and representable combining sequences in spans |
| `clipped_spans` | Styled-span drawing with one shared column budget |
| `textinfo` | Headless text measurement and prefix clipping; printable ASCII always supported |
| `grapheme_boundaries` | Optional Unicode 17 boundary policy for text queries and fields |
| `text_positions` | Headless mapping between source byte offsets and displayed columns |
| `text_wrapping` | Bounded headless column wrapping with original source ranges |
| `wide_text` | Locale-based multibyte measurement/clipping, independent of wide curses support |

Read the array as a set: order is unspecified, and future names should be ignored
unless understood. A listed feature can still fail at runtime, for example when
`geometry` has no controlling terminal. These names describe compiled support;
they do not report terminfo data, negotiated protocols, or ABI compatibility.
The array is unchanged by initialization, resizing, and `end`. Reading it emits
no output, consumes no input, and changes no terminal state. It can also be loaded
alone with `zmodload -F zdraw p:zdraw_features`.

### Terminal geometry

```text
zdraw geometry array
```

Returns a two-element array: **rows, columns**. It queries the controlling
terminal directly, works before `init` and after `end`, and leaves curses window
dimensions and terminal modes alone. There is no default output parameter.

| Status | Meaning |
| --- | --- |
| 0 | Dimensions assigned successfully |
| 1 | Query failed, a dimension was zero, assignment failed, or arguments were invalid |
| 2 | This build lacks `TIOCGWINSZ` |

A failed terminal query leaves the output parameter unchanged. Check the return
status before using it. `position stdscr array` continues to describe the curses
window; `geometry` describes the terminal, which may have changed independently.

The [upstream-format documentation](../Doc/Zsh/mod_zdraw.yo) includes the extension.
The PTY tests exercise live resizing before curses processes input, zero-sized
terminals, local and readonly parameters, argument errors, operation before and
after curses, terminal-mode restoration, no controlling terminal, and existing
window/text/color/refresh operations. Feature tests cover headless discovery,
read-only enforcement, module feature lifecycle, and temporary builds with
optional support disabled and with the preserved stock module.

### Custom borders

```text
zdraw border window [left right top bottom top_left top_right bottom_left bottom_right]
```

The existing `zdraw border window` operation is unchanged. The new form takes
all eight characters; each empty argument selects that edge/corner's curses
default, while a literal space selects a space instead of a border glyph
(curses' normal background-character substitution still applies).
For example, after creating a
window named `panel` in a UTF-8 session:

```zsh
zdraw border panel '│' '│' '─' '─' '╭' '╮' '╰' '╯'
zdraw refresh panel
```

Custom borders preserve the cursor, current window attributes and interior
cells. They inherit the window's drawing attributes and do not refresh the
screen. Windows must have at least two rows and columns. Glyphs must be single
printable characters with system display width one; controls, combining marks,
multi-character strings and double-width characters are rejected before drawing.

Check `custom_borders` in `zdraw_features` before using the new form and
`wide_borders` before selecting Unicode glyphs. Printable ASCII works on narrow
builds. Unicode also requires a suitable locale and Zsh's `MULTIBYTE` option.
Status 0 means success, 1 means invalid arguments or a curses failure, and 2
means a non-ASCII glyph was requested without compiled wide-border support.
A curses failure during drawing can leave a partial border.

Run the showcase with the matching shell, in a UTF-8 terminal of at least
19 rows and 50 columns:

```sh
.build/zsh/Src/zsh -df examples/borders.zsh
```

### Color and character correctness

`ZDRAW_COLORS` and `ZDRAW_COLOR_PAIRS` remain the library's raw counts.
Numeric colors must contain only decimal digits, be below `ZDRAW_COLORS`, and
fit in C's `short` type. Index 255 is valid on a 256-color terminal. Pair IDs
also must fit in `short`; allocation fails before exceeding either that range
or the library limit. Existing pairs remain usable after exhaustion. Named and
numeric spellings retain separate cache entries and their existing `querychar`
readback spelling. Opt-in [truecolor](#truecolor) adds a separate RGB syntax
without widening decimal indices or pair IDs.

The wide-character `char` operation now decodes Zsh's internal string encoding
and passes a terminated buffer to curses, as does the background operation.
`querychar` allocates enough space
for the full curses cell, retaining its existing first-character result when
combining marks are present. Drawing tests cover these paths, custom borders,
deferred refresh, color validation and pair exhaustion, and a temporary module
build using narrow drawing paths.

### Runtime color information

```zsh
typeset -A colors
zdraw colorinfo colors
```

This replaces the named ordinary writable association, creating it if absent.
Other types, readonly or special parameters, and subscripted names are rejected.
There is no default output variable. Status 0 means assignment succeeded; status
1 means invalid arguments or assignment failure. A successful query does not
imply that color drawing is available.

Before `zdraw init` and after `zdraw end`, `initialized` is `0` and all other
fields below are `unknown`. During a session:

| Key | Meaning |
| --- | --- |
| `initialized` | `1` while the module has a curses session |
| `has_colors` | Curses' color capability for the terminal type: `0` or `1` |
| `color_started` | Whether `start_color` succeeded: `0` or `1` |
| `default_colors` | Whether `use_default_colors` succeeded: `0` or `1`; `0` if unavailable or color initialization failed |
| `can_change_color` | Curses' palette-redefinition capability: `0` or `1` |
| `truecolor_supported` | `0` or `1`: this build and initialized terminal description support the RGB interface |
| `truecolor_enabled` | `0` or `1`: the application has enabled RGB color arguments in this session |
| `rgb_min`, `rgb_max` | Inclusive packed RGB range, independent of `color_limit`; `unknown` if unsupported |
| `colors`, `color_pairs` | Raw library counts after successful color initialization; otherwise `unknown` |
| `color_limit` | Number of representable numeric colors: `min(colors, SHRT_MAX + 1)`; indices start at zero |
| `pair_limit` | Available nonzero pair slots: `max(0, min(color_pairs - 1, SHRT_MAX))`; also the highest allocatable pair ID |
| `bg_pair_limit` | Highest pair ID representable by `bg`, accounting for its narrower packed-color path where applicable |
| `query_pair_limit` | Highest pair ID representable by `querychar`, accounting for its narrower readback path where applicable |
| `spans_pair_limit` | Highest pair ID representable by `spans`, accounting for its packed ASCII path; zero if unavailable |
| `pairs_used` | Nonzero pair IDs allocated in this session |
| `pairs_free` | `pair_limit - pairs_used` |

If color initialization fails, module limits and allocation counts are zero,
while raw library counts remain `unknown`. A monochrome terminal can have
`color_started=1` with zero colors; check capabilities and limits as well.
The narrower `bg` and `querychar`
limits apply to **pair IDs**, not foreground/background color indices. Limits
are upper bounds; they do not reserve resources or guarantee a drawing call.
Palette mutability does not establish direct RGB drawing support.

The query reads session state and cached library data. It does not initialize
curses, refresh the screen, allocate pairs, consume input, emit terminal output
or negotiate protocols. Capabilities reflect curses' terminal description.
The legacy count parameters and drawing behavior are unchanged. Applications
can check the `colorinfo` entry in `zdraw_features` before using the command.
Ignore unfamiliar future keys; association order is unspecified.

Repeated queries and repeated `init` calls leave pair allocations alone. Existing
allocation rules still apply: distinct spellings can consume separate slots,
window deletion does not release pairs, and `end` clears them. This also preserves
the existing first-use behavior of `default/default`.

The standalone example captures values before initialization, after initialization,
after allocating a pair, and after cleanup, then prints them with the terminal
restored:

```sh
.build/zsh/Src/zsh -df examples/colors.zsh
```

Tests cover headless and local assignment, readonly/invalid targets, lifecycle
and allocation accounting, monochrome terminals, failed color initialization,
failed or absent default-color support, and narrow builds.

## Styled-span batching

```zsh
zdraw spans panel 1 2 \
  'bold,cyan/black' 'CPU ' \
  'green/black' '23%' \
  '' '  ready'
zdraw refresh panel
```

Supply at least one `style text` pair. Row and column are zero-based, unsigned
decimal coordinates within the window. A style is empty or a comma-separated
list of existing attribute names (`blink`, `bold`, `dim`, `reverse`, `standout`,
`underline`) and at most one existing `foreground/background` color pair.
Tokens cannot be empty or contain whitespace; `+`/`-` attribute changes are not
accepted. Arguments are data and are never evaluated as shell code.

Each style is complete: omitted attributes are off, and an omitted color uses
reserved pair 0. Window/background attributes and background-character
substitution do not affect spans. `default/default` follows the existing color
cache rules; an empty style needs no color support or allocation. Empty texts
are valid, still have their styles validated, and do not allocate colors.

The command preserves the cursor, current attributes, color pair and background.
It neither wraps nor scrolls, even at the bottom-right cell, and does not refresh
or read input. `refresh` remains the application's responsibility. As with other
curses writes, replacing part of an existing wide character can clear its other
cells to avoid leaving an orphaned half-character.

For `spans`, text must fit in the remaining columns of that row; it is never
clipped. Use the separate `spansclip` command for bounded prefix drawing. Tabs,
newlines, other controls, NULs, invalid encoding, and unrepresentable text are
rejected. The `wide_spans` path uses the current locale's system `wcwidth`, with
the `MULTIBYTE` option required for non-ASCII text. A spacing character can have
following zero-width characters within the same span, up to the curses complex
character capacity. A span cannot begin with a zero-width character. This is
curses character handling, not Unicode grapheme segmentation or a guarantee of
emoji/ZWJ rendering. Builds without `wide_spans` accept printable ASCII only.

Status is 0 on success, 1 for invalid arguments, text that does not fit, color
allocation failure or a curses error, and 2 if span drawing is not compiled in
or non-ASCII text needs the unavailable wide path. Check `styled_spans` and
`wide_spans` in `zdraw_features` before selecting an application fallback.

All arguments and text are validated before color allocation or cell changes.
Pairs share the existing session cache and are never recycled. A later
allocation failure leaves earlier successful allocations in the cache, but
leaves window cells unchanged. A curses write error can leave partial drawing;
applications can redraw after failure. `colorinfo[spans_pair_limit]` reports the
highest usable pair ID: the normal `pair_limit` on the wide path, further limited
by packed curses attributes on the ASCII path, or zero when spans are unavailable.
Before initialization and after `end` this value is `unknown`. The narrow path
rejects a pair that exceeds its limit instead of truncating the ID.

## Cell-aware clipping

```zsh
typeset -A info
zdraw textinfo info $'e\u0301界b' 2
# info[text]        = e + combining acute
# info[width]       = 1
# info[remainder]   = 界b
# info[total_width] = 4
# info[truncated]   = 1
```

`zdraw textinfo association text [columns]` measures text and optionally keeps
the longest fitting prefix. It works before `init`, after `end`, and without a
controlling terminal or `TERM`. Omit the budget to measure and return all text.
The named ordinary writable association is replaced, or created if absent, with:

| Key | Value |
| --- | --- |
| `text` | The retained prefix, preserving original bytes |
| `remainder` | The omitted suffix; concatenating it with `text` reconstructs the input |
| `width` | Columns occupied by the prefix under the system-width model |
| `total_width` | Columns occupied by the entire input under that model |
| `truncated` | `1` if any text was omitted; otherwise `0` |

A clipping unit is **one positive-width character followed by all immediately
following zero-width characters**. Widths come from the current locale's system
`wcwidth`, matching the character-width function used by curses, and not Zsh's
optional Unicode width table. Complete multibyte characters are preserved.
A unit is retained only if its positive-width character fits; its zero-width
suffix is retained with it. The first non-fitting unit ends the prefix, even if
some later, narrower character could fit in the leftover space. No partial wide
character, padding, ellipsis, replacement character or normalization is inserted.
The caller can choose such presentation policies in Zsh.

Text must be printable and start with a positive-width character, or be empty.
Tabs, line breaks, other controls, NULs, invalid encoding, and leading zero-width
characters are rejected. A zero budget is valid and retains no nonempty text.
The entire input is validated, including any omitted suffix. Budgets are unsigned
decimal integers from zero through `INT_MAX`; an input whose total width exceeds
`INT_MAX` is rejected. Invalid input leaves an existing association unchanged and
does not create a missing one. Readonly/special parameters, subscripts and other
parameter types are rejected, following `colorinfo`'s assignment contract.

Status is 0 on successful measurement or clipping, 1 for invalid input or
assignment failure, and 2 for non-ASCII text on a build without `wide_text`.
Multibyte text requires a suitable locale and the `MULTIBYTE` option. Without
`wide_text`, printable ASCII is supported. With that feature but an unsuitable
locale or `MULTIBYTE` unset, non-ASCII text fails with status 1. `wide_text` is
independent of `wide_spans`: being able to measure text does not establish that
the linked curses library can draw it. Measurement does not enforce curses'
complex-character storage limit; styled drawing does.

### Shared grapheme-safe native width policy

Use `zdraw textpolicy association [policy]` to discover the width contract without
initializing curses. The opt-in policy
`unicode-17.0.0-egc-wcwidth-sum-attach-zero` shares Unicode 17 boundaries, summed
libc widths and native-storage validation across `textinfo`, `textpos`, `spans`,
`spansclip` and `string`. Whole units are clipped; every unit uses its first
scalar's style, including when it crosses adjacent spans. It does not force
emoji to two cells.

```zsh
typeset -A contract measured
typeset policy=unicode-17.0.0-egc-wcwidth-sum-attach-zero
zdraw textpolicy contract "$policy" || return $?
zdraw textinfo measured '👩‍💻X' 4 "$policy"
zdraw spansclip panel 0 0 4 "policy=$policy" bold '👩‍💻X'
zdraw string panel '👩‍💻X' "policy=$policy"
```

Queries take the policy as their final positional argument. Drawing takes
`policy=NAME` before the first style/text pair, or after the text for `string`.
`text_policy` advertises discovery, and `grapheme_safe_text` compiled support;
check `contract[grapheme_available]` for current locale/option support. Unsupported
policy names, builds, locales and native character groups return 2 without
silently falling back. Safe strings are preflighted single-row writes with
checked cursor advancement; legacy strings keep their inherited behavior.

See the [complete width contract](grapheme-width-policy.md) for discovery fields,
limits, first-scalar styles, literal spaces, error behavior, retained-cell
accounting and caller responsibilities for region boundaries. The existing
query-only `grapheme` policy below does not validate native storage. Default
queries retain the original measurement behavior described above.

### Text positions and hit-testing

```zsh
typeset -A hit
zdraw textpos hit $'e\u0301界b' column 2
# hit[text] = 界; columns [1, 3); original UTF-8 bytes [3, 6)
zdraw textpos hit $'e\u0301界b' byte 4
# The same group, even though byte 4 is inside its UTF-8 encoding.
```

An optional final `grapheme` argument groups Unicode sequences for editing;
`textinfo` accepts it after an explicit column budget. Both retain system cell
widths and default to the original policy. See the [Unicode boundary guide](unicode-boundaries.md)
for field opt-in, data provenance, limits and observed terminal differences.

`zdraw textpos association text column|byte offset` maps a position to the
complete clipping unit containing it. Its unit and validation rules are the
same as `textinfo`: one positive-width character followed by its zero-width
characters. Either column of a double-width character selects the entire unit;
any byte of a base or its following marks selects that same unit.

All offsets are **zero-based**, and range ends are **exclusive**. Byte offsets
count the original encoded bytes, not Zsh character indices or internal escapes.
The ordinary writable association is replaced, or created if absent, with:

| Key | Meaning |
| --- | --- |
| `text` | The complete selected unit, preserving its original bytes |
| `prefix`, `remainder` | Text before and after the unit; `prefix + text + remainder` reconstructs the input |
| `byte_start`, `byte_end` | Source byte range of the unit |
| `column_start`, `column_end` | Displayed column range of the unit |
| `total_bytes`, `total_width` | Length and width of the entire input |
| `at_end` | `1` at the end boundary; `0` for a selected unit |

An offset exactly equal to the chosen total returns an empty range at the end,
with all input in `prefix`. Thus offset zero is valid for empty text. An offset
past the end fails; it is not clamped. Offsets, total bytes and total width must
fit in `INT_MAX`. Decimal offsets are parsed literally, without shell evaluation.

The entire input is checked before assignment, including text after the hit.
Status 0 means success, 1 means invalid text/arguments or assignment failure,
and 2 means non-ASCII text on a build without `wide_text`. Invalid input leaves
the target unchanged. Like `textinfo`, this query works without initialization
or a terminal, reads no input and emits no terminal output. It scans the input
once per query and returns one range, without constructing a full position map.

These are the module's clipping units, **not Unicode grapheme clusters**. A
zero-width joiner stays with its preceding base; the next positive-width
character starts a new unit. The query does not model terminal emoji shaping,
normalize text or enforce curses' combining-character storage limit. Results
use the current locale and `MULTIBYTE` setting; recompute after changing those
or the text. To hit-test a clipped display, query the prefix returned by
`textinfo`, subtracting the drawing origin from the mouse's screen coordinates.

The [hit-testing example](../examples/hit-test.zsh) combines prepared drawing,
structured events and this query. Arrow keys move the selected column; optional
mouse input selects a complete unit:

```sh
.build/zsh/Src/zsh -df examples/hit-test.zsh
.build/zsh/Src/zsh -df examples/hit-test.zsh --mouse
```

### Wrapped text and source ranges

```zsh
typeset -A wrapped
zdraw textwrap wrapped $'ăe\u0301界b' 3
# Row 0: ăé, width 2, original bytes [0,5), original columns [0,2)
# Row 1: 界b, width 3, original bytes [5,9), original columns [2,5)
print -r -- "$wrapped[0,text]"
```

`zdraw textwrap association text columns` greedily wraps **one printable logical
line** into complete clipping units. Each positive-width character keeps its
following zero-width characters, using the same decoder and width policy as
`textinfo` and `textpos`. A row ends before the next unit that would exceed the
budget. Spaces are retained exactly; words can cross row boundaries. Tabs,
newlines and other controls are rejected. Applications own logical-line splitting,
word-breaking and indentation policy.

The budget is a positive literal decimal integer through `INT_MAX`. A unit wider
than the budget fails the whole query; it is never split, dropped or placed on an
overwide row. Empty text produces one empty row. An exact fit does not produce
an extra trailing row. The query accepts at most **1,048,576 original bytes**
and returns at most **4,096 rows**. These limits bound temporary result storage,
including internal escaped strings and hash entries, rather than exact peak bytes.

The ordinary writable association is replaced, or created if absent, with:

| Key | Meaning |
| --- | --- |
| `format` | `zdraw-textwrap-1` |
| `columns` | Requested per-row column budget |
| `line_count` | Number of output rows, including one for empty text |
| `total_bytes`, `total_width` | Original encoded byte length and unwrapped display width |
| `byte_limit`, `line_limit` | Compiled input-byte and output-row limits |
| `N,text` | Original text belonging to zero-based row `N` |
| `N,byte_start`, `N,byte_end` | Half-open source byte range for row `N` |
| `N,column_start`, `N,column_end` | Half-open column range in the unwrapped source |
| `N,width` | Display width of row `N`; the difference between its column endpoints |

Byte offsets count original encoded bytes, not Zsh character indices or internal
escapes. Concatenating row texts in order reconstructs the input exactly. The
ranges are contiguous even when a row leaves unused columns before a wide unit.
For a hit in row `N`, add its `column_start` to the row-local column and query
the original text with `textpos`; validate against `N,width` first so a click
in unused space does not select the next row. Source byte offsets can also keep
a selection anchored while reflow changes the row boundaries.

The complete result is prepared before assignment. Invalid text, excess rows or
bytes, and invalid arguments leave the destination unchanged or absent. Status
is 0 on success, 1 for those errors or assignment failure, and 2 for non-ASCII
input on a build without `wide_text`. Check `text_wrapping` for the command.
It works without `init` or a terminal, reads no input, changes no drawing state,
and never presents a pending frame. It scans the input once and stores only row
ranges and returned text, with no persistent module cache.

These are clipping units, not grapheme clusters or Unicode word boundaries.
Widths follow the current locale and `MULTIBYTE` setting; terminal shaping may
differ. As with `textinfo`, analysis does not enforce curses' combining-character
storage limit. Recompute when the text, width or locale changes.

Run the [reflow example](../examples/wrapping.zsh) with the matching shell:

```sh
.build/zsh/Src/zsh -df examples/wrapping.zsh
```

`+`/`-` adjust width, up/down select a row, and `q` exits. Terminal resizing
reflows the paragraph while retaining the selected source byte. The example
also handles terminals too small to display the body.

### Clipped styled spans

For drawing a composite stream of complete style/text runs:

```zsh
zdraw spansclip panel 1 2 12 \
  'bold,cyan/black' 'Status: ' \
  'green/black' 'ready and waiting'
zdraw refresh panel
```

`zdraw spansclip window row column columns style text [style text ...]` uses
one budget across all spans. The effective budget is the smaller of `columns`
and the space to the window's right edge. Coordinates must be inside the window,
even for a zero budget. Styles, cursor/background preservation, status codes and
refresh behavior follow `spans`; ordinary `spans` retains its overflow error.
Unused columns and cells after the prefix are left alone, subject to curses'
normal repair of an overwritten wide character. Clearing stale text is the
application's responsibility.

Each nonempty span must start with a positive-width character. Keep a base and
its zero-width suffix in the **same span**, even when styles on adjacent runs
match; this avoids assigning conflicting styles within one curses complex
character. All text and styles are validated before drawing, including clipped
runs. Unrepresentable combining sequences are rejected rather than silently
truncated by curses. Entirely omitted or empty spans allocate no color pairs.
A later allocation failure can retain earlier successful allocations but changes
no cells; a curses write error may partially draw, as with `spans`.

These are **cell-width rules, not grapheme boundaries**. Emoji ZWJ sequences,
regional-indicator flags and skin-tone sequences can be split between their
positive-width characters. Zero-width variation selectors stay with the preceding
character but do not alter the width reported by `wcwidth`. Ambiguous-width
characters follow the system locale's policy. A terminal emulator's shaping or
font policy can differ from this model. The default cell-width path does not use Unicode segmentation tables or
terminal-width probing and does not promise grapheme-safe rendering. Separate
opt-in [grapheme boundary queries](unicode-boundaries.md) use generated Unicode data. Neither query nor drawing consumes input or refreshes the terminal.

Run the headless example using the matching staged shell:

```sh
.build/zsh/Src/zsh -df examples/clipping.zsh
```

## Truecolor

For automatic palette selection, hex-to-indexed fallback and reusable ramps,
see the optional [Zsh color companion](colors.md). The strict native interface
below retains its existing opt-in and rejection behavior.

Start the **matching built shell** with a direct-color terminal description that
is appropriate for the actual terminal. For an xterm-compatible terminal that
supports the entry's RGB sequences, and with `xterm-direct` installed:

```sh
TERM=xterm-direct .build/zsh/Src/zsh -df examples/truecolor.zsh
```

Inside an initialized session:

```zsh
zdraw truecolor on || return
zdraw attr panel 'bold' '#80c0ff/#181818'
zdraw bg panel '#e0e0e0/#181818'
zdraw spans panel 1 2 'bold,#80c0ff/#181818' 'RGB text'
zdraw refresh panel
```

The `truecolor` subcommand takes exactly `on` or `off`. It returns 0 on success,
1 for invalid arguments or use outside a curses session, and 2 when `on` cannot
be supported by the current build/library/terminal description. `off` succeeds
within any session. RGB starts disabled; repeated `init` preserves the current
setting, and `end` resets it. `colorinfo` reports support and enabled state
separately. Capability reporting itself does not opt in.

With RGB enabled, either side of a color pair accepts exactly `#RRGGBB`, with
six hexadecimal digits in either case. Quote color arguments. RGB can be mixed
with existing named colors, decimal indices and `default` where supported.
`attr`, `bg` and `spans` share the implementation and cache; subsequent character,
string and border drawing uses the selected pair as usual. `querychar` returns
the cached pair spelling, including hex case. Existing incremental-error behavior
of `attr` and validation behavior of `bg`/`spans` are preserved.

This interface requires optional ncurses extended-color functions, successful
color initialization, exactly 16,777,216 advertised colors, and an `RGB`
capability describing eight bits per channel (Boolean, numeric `8`, or string
`8/8/8`). A library color-content query confirms the encoding without allocating
pairs or writing to the terminal. `COLORTERM`, a large color count alone, palette
redefinition capability, and a guessed `TERM` name do not establish support.
A listed compiled `truecolor` feature can still be unavailable at runtime.

**Reserved low values:** some direct-color entries, including `xterm-direct`,
interpret values 0–7 as ANSI palette indices. The module reads the entry's `CO`
reservation, defaulting conservatively to eight when absent. RGB values below
`rgb_min` are rejected, including `#000000` when the minimum is eight; `black`
still selects the entry's palette black, whose RGB value may differ. An entry
with explicit `CO#0` and corresponding direct RGB setters can represent the
entire range, including exact black and `#000001`. The module trusts the selected
terminal description; it does not probe or rewrite it. Test fixtures cover both
encodings and inspect their emitted SGR bytes.

RGB values are carried as integers to `init_extended_pair`; allocated pair IDs
retain the existing `SHRT_MAX` bound. Decimal color indices retain their existing
`color_limit` even while RGB is enabled. Background, span and readback paths keep
their additional pair-ID limits. Different spellings can allocate distinct pairs;
pairs are never recycled and exhaustion fails normally. No palette approximation
or palette redefinition is performed. Applications choose their own fallback
when enabling RGB or requesting a particular color fails; drawing commands return
status 1 for rejected RGB arguments or allocation failure.

`truecolor off` rejects further RGB arguments, including cache hits, but leaves
existing cells, pairs and window styles intact. It does not recolor the screen;
normal drawing can still use an existing window style until the application
changes it. Re-enabling can reuse retained pairs. `end` releases the session's
pair cache and restores terminal state through the existing curses cleanup.

The subcommand and capability checks do not consume input, emit terminal replies,
refresh pending drawing, or alter input ownership. Input stays with the existing
`zdraw input` API. There is no additional negotiation or reply parser. All
screen output remains within curses and its retained-screen refresh machinery.

## Moving and resizing windows

```zsh
zdraw addwin floating 6 30 2 4
zdraw spans floating 1 1 bold 'Retained window content'
zdraw movewin floating 4 8
zdraw resizewin floating 8 40
# Resize and reposition together, useful after the terminal shrinks:
zdraw resizewin floating 5 24 1 2
zdraw stage stdscr floating
zdraw present
```

`zdraw movewin window row column` moves an ordinary window's screen origin.
It preserves dimensions, retained cells, the logical cursor and drawing/input
settings. This is distinct from inherited `move`, which moves the cursor inside
a surface. Coordinates are zero-based nonnegative literal decimals, and the
entire window must fit inside the current curses screen.

`zdraw resizewin window rows columns [row column]` changes a window's dimensions
and optionally its origin in the same operation. Dimensions are positive literal
decimals, at most **32,767 each**; both the old and requested window are limited
to **262,144 cells** for this operation. Both optional coordinates must be supplied
together. Omitting them keeps the existing origin. The resulting rectangle must
fit entirely inside the current curses screen. Use `resize ... nosave` to update
the terminal dimensions first; it remains separate from `resizewin`.

These operations currently accept **independent ordinary windows without
children**. They reject `stdscr`, pads, subwindows and windows with subwindows.
This avoids silently changing relationships between shared backing storage.
Delete or otherwise rebuild shared window layouts explicitly. Use `resizepad`
for public pad dimensions.

Resizing keeps the upper-left overlap, subject to native wide-character edge
repair, and fills new cells with the window's existing background character and
background attributes. It does not apply the current drawing style to newly
exposed cells. Shrinking discards cells permanently; growing again does not
restore them. The cursor is preserved where possible and clamped independently
to the last valid row and column when necessary. Current attributes, full color
pair, background, input timeout and scrolling setting are retained. Existing
RGB state survives resizing even with `truecolor` disabled.

The implementation duplicates the window privately, resizes and repositions the
copy, restores drawing/input state, and replaces the live window only after all
preparation succeeds and the old window is released. Recoverable allocation,
resize, repositioning or setup failures leave the original handle and retained
contents intact. A failed release of the original window also keeps that handle.
The limits bound old and new cell areas, not exact peak memory: resizing needs
temporary duplicate/reallocation storage as well as the live window. The handle
name and its registry position remain unchanged.

Neither operation stages a frame, reads input or presents output. Already queued
screen cells stay where they were; moving/resizing does not erase an old screen
footprint or update earlier staging. Recompose the background and affected
surfaces, then call `present`. Subsequent ordinary input or inherited `refresh`
still has its usual presentation behavior. Use no-refresh input where supported
when the application owns the presentation boundary.

A width reduction can intersect a stored wide character. The linked library's
`wresize` repair behavior applies; the module does not provide independent
Unicode boundary reconstruction. Align cuts to complete characters where
possible, and redraw layout-dependent content such as borders after resizing.
See the [window movement contract](https://invisible-island.net/ncurses/man/curs_window.3x.html)
and [resize contract](https://invisible-island.net/ncurses/man/wresize.3x.html).

Check `window_movement` for compiled `mvwin` support. `window_resize` requires
`wresize`, `mvwin`, `wattr_get` and `wattr_set`. The latter two preserve the full
current style, including high pair IDs that some `dupwin` implementations lose.
Both commands require `init`; status is 0 on success, 1 for invalid arguments,
unsupported surface relationships, bounds, resource limits or library failure,
and 2 for missing compiled support.

Run the [floating-window example](../examples/windows.zsh) with the matching shell:

```sh
.build/zsh/Src/zsh -df examples/windows.zsh
```

Arrows move the retained window, `+`/`-` resize it, and `q` exits. Terminal resizing
clamps its geometry; very small terminals temporarily show only the background.

## Offscreen pads and composed frames

```zsh
zdraw addpad document 200 120
zdraw spans document 50 10 bold 'A retained document row'

# Compose back to front, then display once.
zdraw stage stdscr
zdraw viewport document 50 10 2 4 12 60
# Optional ordinary windows can be staged above the viewport:
# zdraw stage popup
zdraw present
```

`zdraw addpad name rows columns` creates a blank offscreen curses surface. It
shares the ordinary window namespace and appears in `zdraw_windows`. A pad can
be larger than the terminal. Dimensions must be positive literal decimal
integers, at most **32,767 each**, with at most **262,144 cells per pad** and
**1,048,576 cells across live public pads**. Invalid arguments, duplicate names
and failed allocations do not register a handle or consume this budget.

These are cell limits, not an exact memory quota: curses' per-cell and per-row
storage varies. They exclude ordinary windows, prepared rows, snapshot results
and temporary copy/input pads. A pad retains every allocated cell; it does not
virtualize an unlimited dataset. Applications choose what to materialize.

Existing drawing operations work in pad coordinates: `move`, `char`, `string`,
`spans`, `spansclip`, prepared `draw`, `fill`, `restyle`, `copy`, `attr`, `bg`,
`border`, `clear` and `scroll`. `cellinfo`, `querychar` and `snapshot` inspect
retained pad cells. Their existing limits still apply: a pad larger than the
snapshot cell limit cannot be captured whole. `position` retains its six-field
array format; the screen-origin fields are **-1, -1** for a pad. The cursor and
dimensions are real pad coordinates and dimensions.

`zdraw viewport pad pad_row pad_column screen_row screen_column rows columns`
stages a rectangular view into curses' virtual screen. Coordinates are zero-based
nonnegative literal decimals, dimensions are positive, and both rectangles must
fit completely. Bounds use the current curses screen dimensions; handle resize
events and call `resize ... nosave` before choosing the next viewport. There is
no automatic clipping, expansion or negative-coordinate normalization.

`zdraw stage window [window ...]` stages complete ordinary windows in order.
The entire argument list is validated before any staging. Both `stage` and
`viewport` mark their contributing rows for copying, so an unchanged surface
can cover a previously staged overlay. Later contributions determine the
composed cells in overlapping regions, subject to native wide-character rules.
One pad may contribute multiple views to the same frame. Moving a viewport does
not erase its previous location: stage the background or another surface there.

`zdraw present` calls curses' final screen update without adding another window
to the frame. Staging never presents by itself. It preserves retained text,
styles, backgrounds and logical cursors, but changes curses' dirty markers and
queued screen state. The physical cursor follows curses' composition rules.
Repeated `present` calls use the retained virtual screen; there is no implicit
frame reset. The curses screen diff still determines terminal output.

The inherited `refresh` behavior remains available for ordinary windows.
`refresh window ...` also displays any already-staged contributions; bare
`refresh` adds `stdscr` first and can cover a queued viewport. Likewise, default
input can refresh its target window. Use `event stdscr event norefresh` when
available to preserve explicit presentation during input. `stage`/`present`
do not provide synchronized-output protocols or atomic terminal paint.

Pads reject `input`, `event`, `timeout`, ordinary `refresh` and `stage`; use an
ordinary input window and `viewport` for presentation. `addwin ... parent`
rejects a pad parent; subpads are not exposed in this milestone. Existing
`resize` changes the terminal screen, not pad dimensions. Use `resizepad` to
change a pad's allocated extent explicitly.
No viewport mapping is saved by the module for automatic redisplay after resize.

Delete a pad with `delwin`. This releases its budget but does not erase or
cancel previously staged screen cells. A pad deletion failure retains its handle
and budget so it can be retried. `end` and module unload release public pads
through the existing cleanup path. No extra input reader or terminal protocol
is introduced.

Align viewport edges to complete wide characters where possible. Curses owns
wide-cell clipping and overlap behavior; the module does not reconstruct glyph
boundaries or promise portable repair of split characters. See the
[curses pad contract](https://invisible-island.net/ncurses/man/curs_pad.3x.html).

Check `offscreen_pads` for compiled `newpad` and `pnoutrefresh` support, and
`staged_refresh` for `stage`/`present`. All four commands require `init`.
Status 0 means success; 1 covers invalid input, limits, allocation or library
errors; `addpad` and `viewport` return 2 when compiled pad support is absent.
Validation failures do not change queued screen cells. A library failure during
staging can leave partial queued contributions; a failed `present` can leave
partial terminal output. Neither operation rolls back the frame.

Run the [viewport example](../examples/viewports.zsh) with the matching shell:

```sh
.build/zsh/Src/zsh -df examples/viewports.zsh
```

Arrows pan a retained document, `+` adds 20 rows, `-` truncates 20 rows, space
toggles an ordinary overlay, and `q` exits. Growth populates only the newly
allocated rows; truncation discards their drawing. The example generates its
row data and limits the surface to 20–400 rows. Screen resizing recomposes the
visible area without rebuilding the pad.

## Resizing pads

```zsh
zdraw addpad document 100 120
zdraw spans document 10 0 bold 'Retained document content'
zdraw resizepad document 200 120
# Application data can now populate the extra rows.
zdraw spans document 150 0 '' 'New content'
zdraw stage stdscr
zdraw viewport document 145 0 1 0 10 60
zdraw present
```

`zdraw resizepad pad rows columns` changes a public pad's allocated dimensions
without changing its handle or registry position. It rejects ordinary windows
and `stdscr`. Dimensions are positive literal decimal integers, with the same
**32,767 per dimension**, **262,144 cells per pad**, and **1,048,576 live public
pad cells** limits as `addpad`. The old pad is credited against the session
budget: a same-area resize can succeed at the full budget, and a successful
shrink releases cells for other pads. It can exceed the terminal dimensions.

The upper-left overlap survives, subject to native wide-character edge repair.
New cells use the existing background character and attributes. Shrinking
discards cells permanently; growing again does not restore them. The logical
cursor is clamped independently to the last valid row and column. Current
attributes, full color pair, complex background and scrolling mode are retained.
Existing RGB state survives with `truecolor` disabled, without allocating pairs.

A private, genuine curses pad receives the original cells and background before
native `wresize` changes its dimensions. This avoids relying on `dupwin` to retain
pad identity across curses implementations. The original is released and its
registered pointer and accounting updated only after preparation succeeds.
Allocation, copying, resize, setup or original-release failures retain the old
pad and budget. Temporary pad/reallocation storage is additional to the live
cell budget; these limits do not specify exact peak memory. The public `copy`
operation's smaller cell limit does not restrict `resizepad`.

Resizing does not stage, read input or present. Already queued screen cells
survive even when their source rows are truncated. Choose valid viewport bounds,
recompose the background and affected surfaces, and call `present` to display
the new layout. No viewport mapping is retained or adjusted automatically.
Resized pads remain inputless, with the same restrictions as `addpad` surfaces.

When shrinking through a wide character, the linked library's
[`wresize` behavior](https://invisible-island.net/ncurses/man/wresize.3x.html)
applies. Align cuts to complete characters and redraw layout-dependent content
where needed; there is no independent Unicode reconstruction.

Check `pad_resize`: it requires pad support plus `copywin`, `wresize`,
`wgetbkgrnd`, `wbkgrndset`, `wattr_get` and `wattr_set`. `init` is required.
Status is 0 on success, 1 for invalid arguments, limits or library failure,
and 2 for missing compiled support. The [viewport example](../examples/viewports.zsh)
demonstrates growth, truncation and scroll-position clamping in Zsh.

## Styled rectangle fills

```zsh
# Literal styled spaces for a panel background:
zdraw fill stdscr 2 4 6 24 blue/black ' '
# Horizontal and vertical rules are one-row or one-column rectangles:
zdraw fill stdscr 8 4 1 24 bold '-'
zdraw fill stdscr 2 28 7 1 bold '|'
```

`zdraw fill window row column rows columns style tile` fills a rectangle by
repeating one complete, single-column cell. The tile may include following
combining marks on a wide drawing build. Empty text, multiple spacing characters,
wide two-column characters, leading combining marks and controls are rejected.
Style syntax, color allocation and combining-character storage limits are shared
with `spans`. Printable ASCII tiles work with the narrow writer; non-ASCII tiles
require `wide_spans`. RGB styles require the existing truecolor opt-in.

Coordinates are zero-based and dimensions are **positive** decimal integers.
Zero dimensions are rejected. The entire rectangle must fit inside the window;
there is no clipping, implicit expansion or coordinate-expression evaluation.
Geometry and the complete tile/style are validated before any drawing. Invalid
geometry, text or styles do not allocate colors or change cells.

The operation compiles the tile once, builds one repeated row, and reuses that
row for each write. It preserves the cursor, current attributes and background,
does not wrap or scroll, and leaves presentation to `refresh`. A space tile is a
literal space, independent of the window's background character. Bottom-right
cells can be filled normally. No prepared object is created or retained.

Replacing part of an **existing wide character** follows the linked curses
array writer's behavior, just as with `spans`. Repairs to that character can
affect its occupied cells outside the rectangle. Align boundaries to complete
characters when surrounding wide text must be preserved; use `textpos` with the
original text to determine those boundaries. The rectangle's dimensions alone
do not provide a strict mutation boundary for intersecting wide characters.
Tests compare these cases directly with ordinary span writes.

Status 0 means all rows were written. Status 1 covers invalid arguments, color
allocation or a library write failure. Status 2 means the compiled writer is
unavailable, or a non-ASCII tile requires wide support. A write failure can leave
previous rows, and potentially part of the failing row, changed; the operation
does not roll back drawing. The shared writer restores cursor/style state when
its library operations permit that, including the tested write-error path.

Run the [region example](../examples/regions.zsh) with the matching shell. Arrow
keys move the filled rectangle, space changes its tile, and `q` quits:

```sh
.build/zsh/Src/zsh -df examples/regions.zsh
```

The [fill benchmark](../benchmarks/README.md#rectangle-fills) compares a fill with
ordinary and prepared row loops using equal content and explicit refresh.

## Copying retained regions

```zsh
# Scroll a ten-row, forty-column area upward; draw the exposed line separately.
zdraw copy stdscr 3 4 stdscr 2 4 9 40
zdraw fill stdscr 11 4 1 40 '' ' '
zdraw refresh stdscr
```

`zdraw copy source source_row source_column destination destination_row
 destination_column rows columns` copies an **opaque** rectangle of retained
cells. Spaces overwrite destination cells just like other characters. Stored
attributes, color pairs and combining marks travel with the cells; the
operation does not parse text, apply the destination background, allocate
colors or require truecolor to remain enabled for existing RGB pairs.

Coordinates are zero-based, nonnegative decimal integers; dimensions are
positive decimal integers. Both rectangles must fit completely in their
windows. There is no automatic clipping, resizing or coordinate evaluation.
At most **65,536 occupied columns across all rows** may be copied per call.
Larger copies must be divided by the application, which must also account for
overlap between its separate calls.

A private pad holds the requested source rectangle before the destination is
changed. Self-overlap works in every direction, and differently named windows
sharing parent/subwindow storage are handled the same way. The pad is released
before returning; no reusable object or new public window is created. Cursors,
current drawing attributes and backgrounds stay unchanged. The copy does not
wrap, scroll, read input or refresh. Shared subwindow content changes normally;
use the inherited `touch` operation on another view before refreshing that view
when curses requires its change markers to be synchronized.

This operation uses opaque curses `copywin` calls. **Align horizontal edges to
complete characters in both source and destination.** It copies occupied
columns without decoding or reconstructing wide-character layout. Edges that
split an existing wide character inherit the linked library's behavior and can
leave partial retained glyphs; no portable repair or terminal appearance is
promised. This also applies to a subwindow whose own edge splits a parent glyph.
Use `textpos` with original text when determining character-aligned boundaries.
See the [curses copy contract](https://invisible-island.net/ncurses/man/curs_overlay.3x.html).

Check `region_copy` for compiled `copywin` and `newpad` support. Status 0 means
the copy and temporary-pad cleanup succeeded; 1 means invalid arguments,
resource limits, allocation or library failure; 2 means compiled support is
unavailable. Invalid arguments, allocation failure and source-staging failure
leave destination cells untouched. A destination-write failure can leave a
partial copy; there is no rollback. A cleanup failure also returns 1 even if
the destination copy completed.

Run the [scrolling example](../examples/copy.zsh) with the matching shell:

```sh
.build/zsh/Src/zsh -df examples/copy.zsh
```

Up/down scroll retained rows; only the newly exposed row is drawn. Resizing
rebuilds the view, and `q` exits. This illustrates reuse; it does not establish a
performance advantage over prepared drawing for every workload.

## Restyling retained text

```zsh
# Highlight an existing row without supplying its text again.
zdraw restyle stdscr 3 2 1 30 bold,reverse
# Restore a known base style when the selection moves.
zdraw restyle stdscr 3 2 1 30 ''
zdraw refresh stdscr
```

`zdraw restyle window row column rows columns style` replaces the stored colors
and attributes of a rectangle, retaining its character data, including spaces
and combining marks. It uses the same **complete style** syntax as `spans`:
comma-separated attribute names and at most one foreground/background pair.
An omitted color means pair zero, and an empty style clears all attributes and
uses pair zero. This is replacement, not an attribute toggle or a saved-style
stack. Applications own the styles they restore; `+bold` and `-bold` are invalid.

Coordinates are zero-based, nonnegative decimal integers and dimensions are
positive decimal integers. The entire rectangle must fit. Invalid geometry or
style is rejected before color allocation or cell changes. RGB color arguments
require the existing truecolor opt-in, including cache hits. Restyling with a
new complete style replaces any existing RGB pair in the rectangle.

`restyle` makes one curses `wchgat` call per row. It does not decode text, build
a cell buffer, wrap, scroll or refresh. Existing Unicode cells can be restyled
even when the current locale cannot decode their text. The window's current
drawing attributes and background are untouched, and the live cursor is restored
before returning when the library permits it. Parent/subwindows share modified
cells as usual; `touch` another view before refreshing it when its change markers
need synchronization.

Align horizontal edges to complete characters in existing wide text. Updates
follow the linked library's occupied-column behavior and do not repair partial
wide glyphs or guarantee their terminal appearance. Complete style replacement
also clears attributes outside the supported style vocabulary, including the
**alternate-character-set marker** on legacy ACS borders. Stored codes remain,
but their displayed meaning can change; redraw those borders or use explicit
Unicode border characters when appropriate. See the
[curses attribute contract](https://invisible-island.net/ncurses/man/curs_attr.3x.html).

Check `region_restyle` for compiled `wchgat` support; array writers and text
readers are not required. Status 0 means every row and cursor restoration
succeeded; 1 covers invalid arguments, color allocation, movement or library
errors; 2 means compiled support is unavailable. A failed update may leave
previous rows or part of the failing row restyled. Successfully allocated pairs
remain in the session cache. Cursor restoration is attempted after a failed
update, but a restoration failure can leave it at the last operation's position.
No drawing rollback is attempted. Older ncurses ABIs that pack color-pair IDs
reject pairs that do not fit their packed representation.

The [highlight example](../examples/restyle.zsh) draws labels once and moves the
selection by restyling the old and new rows. A resize rebuilds the labels:

```sh
.build/zsh/Src/zsh -df examples/restyle.zsh
```

## Inspecting rendered cells

```zsh
typeset -A cell
zdraw spans stdscr 0 0 bold $'e\u0301'
zdraw move stdscr 0 0
zdraw cellinfo stdscr cell
# cell[text] contains both e and its stored combining acute accent.
# cell[characters] = 2; cell[attributes] = bold
```

`zdraw cellinfo window association` reads the cell at the current window cursor.
It requires an initialized session and replaces an ordinary writable association,
or creates one if absent. It does not move the cursor, touch or refresh the
window, change its drawing style, consume input or allocate colors. It observes
curses' retained cells, including drawing not yet presented to the terminal.

| Key | Meaning |
| --- | --- |
| `text` | Complete text stored in a wide curses cell, including combining marks; the library's byte representation with narrow readback |
| `characters` | Number of stored wide characters; `1` for byte readback |
| `encoding` | `multibyte` or `byte`, identifying the readback path |
| `row`, `column` | Current cursor position, relative to the window and zero-based |
| `attributes` | Space-separated known flags: `blink`, `bold`, `dim`, `reverse`, `standout`, `underline`, and `altcharset` when present |
| `attribute_bits` | Decimal mask of all non-color attribute bits, including flags without a name above; library-specific diagnostics |
| `color` | Original color-pair spelling from the module's cache, or `unknown` |
| `color_source` | `cache` or `unknown`, describing the evidence for `color` |
| `pair` | Curses color-pair ID, valid for this session only |

Wide readback uses `win_wch` and `getcchar`, converts the entire stored string
using the current locale. This is stored character content, not recovery of the
original source encoding. It does not claim
that all characters originally supplied to curses survived its storage limits.
The `MULTIBYTE` option does not alter readback; the active locale must represent
the stored characters. A failed conversion returns status 1 without replacing
the target. Narrow readback reports the library's packed byte cell value, which
cannot reconstruct wide text and has the packed color-pair limit reported by
`colorinfo[query_pair_limit]`; it cannot reliably inspect colors above that limit.
Check `wide_cell_inspection` before requiring complete wide-cell readback.

Color names are cache evidence, not a query of the terminal's palette. A library
can establish the cached `default/default` label even on a monochrome terminal;
use `colorinfo` to discover actual color support. Named and
numeric spellings remain distinct, RGB spellings remain readable after truecolor
is disabled, and an uncached pair returns `color=unknown`. `pair` and
`attribute_bits` should not be compared across libraries or sessions. `altcharset`
identifies alternate-character-set values whose terminal glyph may differ from
`text`.

A wide character can occupy multiple columns. On ncurses, reading an occupied
continuation column returns the same stored text; `row`/`column` still identify
the cursor location. This interface does **not** report the leading column or a
continuation marker, and should not be treated as a cell-layout serialization
format. The snapshot operation below captures these readback values; optional
[occupancy metadata](recording-and-restoration.md) is available for snapshots.

Status is 0 on assignment and 1 on invalid arguments, a failed library read,
encoding conversion or assignment failure. Targets are validated before reading;
readonly/special parameters, indexed arrays, scalars and subscripts are rejected.
Failure preserves an existing target and does not create an absent one.
The inherited `querychar` still returns only the first character in its existing
positional array; its behavior is unchanged.

Run the [inspection example](../examples/cell-inspection.zsh) to draw a prepared
row, inspect several cells, and print quoted records after ending the session:

```sh
.build/zsh/Src/zsh -df examples/cell-inspection.zsh
```

### Window snapshots

```zsh
typeset -A frame
zdraw snapshot stdscr frame
print -r -- "$frame[format]: $frame[rows] rows, $frame[columns] columns"
print -r -- "${(qqqq)frame[0,0,text]}"
```

`zdraw snapshot window association` captures the whole retained window in one
call. It uses an independent temporary copy, so the live window's cursor,
drawing styles and touched/moved state stay unchanged. It does not refresh or
consume input. Subwindows are supported; their coordinates remain relative to
the subwindow. The temporary copy is released before result assignment, including
on a failed read or conversion.

The ordinary writable association is replaced, or created if absent. Its values
are owned by the shell and survive later drawing, window deletion, `end`, and
module unload. The metadata keys are:

| Key | Meaning |
| --- | --- |
| `format` | `zdraw-snapshot-1` |
| `layout` | `readback`: one complete `cellinfo` record for each window coordinate |
| `rows`, `columns` | Captured window dimensions |
| `cursor_row`, `cursor_column` | Live cursor position at capture time |
| `cell_count` | `rows * columns` |
| `cell_limit` | Maximum cells accepted by one capture: 65,536 |
| `byte_limit` | Maximum accounted key/value bytes: 16 MiB |

Every cell field is keyed as `row,column,field`, using zero-based decimal
coordinates. For example, `frame[2,5,text]`, `frame[2,5,attributes]` and
`frame[2,5,color]` describe the cell at row 2, column 5. All ten `cellinfo` fields
are included, with identical meanings and encoding/color limitations. Readback
on ncurses repeats a wide character's stored text at its occupied continuation
columns; the default snapshot does not infer leading cells or grapheme boundaries.
Do not concatenate each coordinate's `text` to reconstruct a rendered row.

An optional final `occupancy` argument adds conservative occupancy, provenance,
base-column, cell-width and supported-style metadata. Wide cells at clipped or
shared edges remain unknown; interior wide runs are explicitly inferred. See
[recording and restoration](recording-and-restoration.md) for these fields
and the separate `zdraw-screen-1` single-column restoration contract.

This versioned record supports inspection and comparisons. It is not a terminal
image, a restore command or a portable binary curses-window dump. To compare
captures, check format/dimensions and selected fields. Pair IDs and raw attribute
bits are library/session diagnostics; cache spellings are not normalized color
identities. Association iteration order is unspecified. Text values should be
quoted when printing diagnostics, as the example above does.

Cell count is checked before duplicating the window. Key/value accounting includes
metadata, internal string escaping and terminating NULs; it does not include
allocator, association-node or temporary-window overhead. Oversized windows or
results fail rather than returning a partial capture. An invalid target, failed
copy/read/conversion, or exceeded budget returns status 1 and leaves an existing
result unchanged; an absent target is not created. Status 0 means assignment
succeeded. No persistent snapshot object is kept in the module.

The [snapshot diff example](../examples/snapshot-diff.zsh) changes one character's
text and attributes and prints the differences after ending the session:

```sh
.build/zsh/Src/zsh -df examples/snapshot-diff.zsh
```

## Structured input

```zsh
typeset -A event
zdraw timeout stdscr 100
if zdraw event stdscr event; then
  case $event[type] in
    character) text=$event[text] ;;
    key)       key=$event[key] ;;
    resize)    rows=$event[rows] columns=$event[columns] ;;
  esac
fi
```

`zdraw event window association [mouse] [norefresh] [poll]` requires an initialized session and
returns one record through an ordinary writable associative parameter. It creates
an absent parameter and replaces an existing association, so fields from a
previous event do not linger. Invalid targets (including readonly, special,
scalar/array and subscripted parameters) are rejected before reading input or
acknowledging a pending size change. No implicit `REPLY` parameter is used.

| Field | Meaning |
| --- | --- |
| `type` | `character`, `key`, `resize`, `mouse`, or opt-in `paste` (separate schema below) |
| `source` | `curses` for decoded input; `terminal` for a detected size change |
| `text` | One decoded character, or one raw byte on a narrow input build; empty for other events |
| `key` | Empty for characters; curses name without `KEY_` for named keys (such as `UP`, `F5`, `RESIZE`, `MOUSE`); decimal code for an unrecognized key |
| `code` | Numeric decoded character or curses key code; `unknown` for a synthesized terminal-size event |
| `encoding` | `multibyte` with wide curses input, `byte` otherwise; `none` for a synthesized size event |
| `modifiers` | `unknown` for characters/keys/resizes; a space-separated set of `SHIFT`, `CTRL`, `ALT` for a mouse event, empty if none were reported |
| `rows`, `columns` | Present only on resize; see the source distinction below |
| `id`, `x`, `y`, `z`, `buttons` | Present only on mouse events |

A `character` record can contain a control character or NUL; it does not claim
that the value is printable or came from an unmodified physical key. Wide input
uses the current locale and returns the numeric wide-character value in `code`;
narrow input returns bytes 0–255. Curses key codes are library-specific, not a
portable enumeration. Legacy decoding cannot reliably distinguish modifiers,
physical keys, press/repeat/release or focus changes. See the separate opt-in
[enhanced input contract](enhanced-input.md) for supported keyboard and focus events.

**Ownership and timing:** `event` and the existing `input` share one curses
input queue and decoder. Applications choose which call consumes the next item;
there is no background reader. Paste parsing is enabled only by `paste on`. Do not concurrently
read the terminal through `read`, ZLE or a subprocess. `event` enables keypad
decoding and inherits `zdraw timeout window milliseconds`. Zero polls; a finite
positive timeout is useful for observing size changes without keypresses.
Curses escape-sequence timing and inherited EINTR retries can extend a wait;
this is not a strict overall deadline. By default, curses may refresh a modified
window during a read, as with legacy input.

`poll` temporarily uses a zero initial timeout and then restores the configured
window timeout. It can be combined with `mouse` and `norefresh`; flags are distinct
and order-independent. Native escape decoding can still wait. See the
[application integration guide](application-integration.md) for `inputinfo`,
`inputdelay`, and a bounded `zselect` loop that accounts for curses' input queue.

With `norefresh`, the call reads through a private one-cell ncurses pad and
**does not refresh drawing windows or present pending drawing**. It still uses
the same input queue, decoder and selected window's timeout; it leaves that
window's cursor and dirty state alone. Present the completed frame explicitly
with `zdraw refresh`. Keypad/mouse setup can emit terminal control sequences:
this is a presentation guarantee, not a promise of zero terminal output.
The pad is allocated lazily, is absent from `zdraw_windows`, and is released by
`end` or module unload. No additional terminal protocol or reader is enabled.

Check `norefresh_events` before requesting the flag. This path is currently
enabled only for ncurses, whose pad input behavior supports the guarantee;
other curses builds return status 2 before consuming input or acknowledging a
resize. The two flags may appear in either order, once each. For example:

```zsh
zdraw event stdscr event norefresh mouse
```

Terminal dimensions are checked before reading and after a failed read. A change
from the last acknowledged size returns `source=terminal`, `key=RESIZE` and
`code=unknown`, without consuming input, resizing windows or refreshing. Changes
between observations can coalesce; dimensions of zero or an unavailable query
are ignored. The initial comparison size comes from session initialization.
A decoded curses `KEY_RESIZE` returns `source=curses` and curses' current screen
dimensions instead. Applications can receive both kinds of notification and
should handle resizing idempotently. `event` installs no signal handler; a size
change during an indefinite read need not wake it immediately.

The optional literal `mouse` requests the existing curses mouse mask. It must
be supplied on each read that wants mouse reporting; an actual curses read
without it disables reporting. A synthetic resize record does not alter the
input modes. `zdraw mouse` configures the mask as before. Mouse `x` and `y` are
zero-based screen coordinates, not coordinates relative to the input window;
`id` and `z` are library-reported values. `buttons` is a space-separated list
such as `PRESSED1`, `RELEASED1` or `CLICKED1`; it may contain multiple states or
be empty for motion. No hit-testing or shortcut policy is added.
Mouse enable/mask flags now use distinct bits and reset at `end`, correcting
legacy bookkeeping so reporting disables and re-enables across sessions.

Status 0 means a record was assigned. Status 1 covers invalid arguments, a
failed read (including timeout/interruption), unavailable mouse data or failed
assignment, or input-pad allocation failure. Status 2 means a requested flag
(`mouse` or `norefresh`) lacks compiled support.
Failed reads leave the target unchanged; input already consumed cannot be
restored if subsequent record assignment fails. An absent target is not created
on an empty poll. Check `structured_events`, `wide_events` and `resize_events`
in `zdraw_features`; the last reports compiled geometry-query or curses resize
notification support, not a guarantee of runtime notification delivery.

Run the [event inspector](../examples/events.zsh), which also reuses a prepared
heading, in the matching built shell and a UTF-8 locale:

```sh
.build/zsh/Src/zsh -df examples/events.zsh
# Opt in to mouse input:
.build/zsh/Src/zsh -df examples/events.zsh --mouse
```

Press `q` to exit. Unicode display needs the wide drawing path. The example uses
`always` for session cleanup; module unload also invokes the existing cleanup.

## Paste, foreground commands and asynchronous output

The final application integration batch adds:

- `zdraw paste on|off`: opt-in streaming bracketed paste. `event` returns
  `type=paste` records with `phase=begin|data|end`, raw `text`, and `bytes`.
  Payload records contain at most 4,096 bytes, including arbitrary binary data.
- `zdraw suspend` / `zdraw resume`: release terminal modes for a foreground
  command, then restore the retained session and repaint. The companion
  `zdraw-run` wrapper uses `always` and preserves the command's exit status.
- `event ... poll`, `inputinfo` and `inputdelay`: compose curses input with worker
  pipes using `zselect`, with explicit decoder timing and a bounded polling tick.
- `lib/zdraw-sgr.zsh`: a bounded SGR-only streaming decoder, returning styled text
  and newline/tab/carriage-return records. Other terminal commands are filtered.

Read the [contracts, limits and cleanup rules](application-integration.md)
before using the new APIs. They preserve one terminal input owner and require
explicit opt-in for paste. `input` is rejected while paste owns the input stream;
finish an active paste before disabling it or handing off the terminal.
Paste enablement also owns raw input mode; applications handle Ctrl-C as input
until paste is disabled. Previous terminal modes are restored on disable/handoff.

The [combined example](../examples/streams.zsh) receives colored output from a worker
pipe while handling keyboard input and paste. `!` hands the terminal to a
foreground command, and `q` exits:

```sh
.build/zsh/Src/zsh -df examples/streams.zsh
```

## Prepared styled rows

```zsh
# Within an initialized session:
zdraw prepare heading 'bold,cyan/black' 'CPU ' 'green/black' '23%'
zdraw draw stdscr 0 0 heading
zdraw draw stdscr 1 0 heading 6  # clip to six columns

typeset -A row
zdraw rowinfo heading row
zdraw unprepare heading
zdraw refresh
```

The `prepared_rows` feature adds:

```text
zdraw prepare name style text [style text ...]
zdraw draw window row column name [columns]
zdraw rowinfo name association
zdraw unprepare name
```

`prepare` validates and decodes a complete row, allocates its nonempty spans'
color pairs and copies the resulting cells into an immutable named object.
Names must be identifiers without subscripts and belong to a module namespace,
not shell parameters. A duplicate name is rejected; release it before reusing
it. Changing the original shell strings does not change a prepared row.
Preparation requires an initialized session and is independent of any window's
size. Empty rows are allowed and still have their styles validated.

Text, complete styles, combining-character storage, color validation and status
2 for unsupported non-ASCII drawing follow `spans`. They share the same compiler
and the same window-state-preserving writer. All text and styles are checked
before allocating colors. A failed later allocation can leave earlier color
pairs cached, but creates no named row and changes no cells. Preparation does
not read input or emit drawing output.

`draw` writes the prepared cells without reparsing styles/text or allocating
colors. Without `columns`, the whole row must fit. With a budget, it draws the
longest prefix fitting that budget and the window's right edge. A base and its
stored combining marks stay together; a double-width cell is never split. This
is still the existing cell-width contract, not full grapheme segmentation.
Unused cells are unchanged, subject to curses' repair of overwritten wide
characters. Coordinates must be in the window even for an empty row or zero
budget. Drawing neither wraps, scrolls, reads input nor refreshes; it preserves
the cursor, window attributes, color pair and background. A curses write error
can leave partial drawing.

Prepared cells are bound to the session, the `LC_CTYPE` locale name and the
`MULTIBYTE` option at preparation. `draw` rejects a changed locale/option even
for an ASCII row; restore the original setting or prepare another row. Cells
hold already allocated color-pair IDs. Like existing window styles, prepared
RGB cells remain drawable after `truecolor off`; preparing new RGB rows still
requires `truecolor on`. Preparation allocates colors for the complete row,
including cells that a later clipped draw might omit.

`rowinfo` also reports `draws`, the number of successful draws of that row
(including empty and zero-budget calls, saturating at `resourceinfo[counter_limit]`).
`rowinfo` reports `width` (terminal columns), `cells` (stored spacing-character
groups, not columns), `bytes`, `session_bytes`, `session_limit`, `locale` and
`multibyte`. Its target follows `colorinfo`'s ordinary-association rules and is
unchanged for an unknown row. The session allows 16 MiB of accounted prepared
row storage, including object records, names, locale names, cells and widths.
Hash-table/allocator overhead, transient compilation buffers and the shared
color cache are not included. Preparation checks a conservative capacity bound
based on column width before allocating colors; a wide row can therefore be
rejected even if its eventual stored-cell count would use fewer bytes.

`unprepare` frees the named row without changing drawn cells or reclaiming its
color pairs. `end` and module unload free every prepared row; a subsequent
session starts empty. Repeated `init` within the same session keeps rows.
All four commands require initialization. Status 0 is success, 1 is invalid
input, unknown/duplicate name, locale mismatch, storage/color failure or curses
failure, and 2 is unavailable compiled drawing support (including non-ASCII text
on the narrow preparation path).

Run `python3 benchmarks/spans.py --prepared` to compare repeated drawing with
legacy calls and ordinary spans. See [the benchmark notes](../benchmarks/README.md)
for the reuse workload and measured results.
