# Optional UI toolkit

The visual toolkit supplies themes, composable styling utilities, panels, labels,
scrolling lists, tables, editable forms and semantic documents. Each component is
an optional Zsh library.
Applications choose their rectangles, data, keymaps and event loops.
Pure layout helpers now handle fixed/flexible tracks, gaps, insets and centering;
the [list/detail recipe](recipes/list-detail.md) demonstrates responsive composition.

For a visual entry point, [Choose and combine TUI pieces](choose-and-combine.md)
maps reader tasks to existing libraries and experimental treatments, with their
space requirements, shared styling and application-owned connections.

This is the first implementation from the
[beautiful TUI research](beautiful-tuis-research.md). It uses ordinary Zsh
arguments and arrays for a Tailwind-inspired workflow: share a theme, combine
utilities into reusable variants, and override individual instances.

## Try the gallery

After the [reproducible build](building.md#build-and-test), run from the
repository root in a terminal:

```sh
.build/zsh/Src/zsh -df examples/gallery.zsh
```

The [gallery source](../examples/gallery.zsh) is a complete application showing
how to compose the libraries. It uses the matching built shell and staged module.

| Key | Effect |
| --- | --- |
| `t` | Switch dark/light theme. |
| `b` | Cycle rounded, double, ASCII and borderless panels. |
| `d` | Switch comfortable/compact padding. |
| Tab | Switch focused/inactive selection. |
| `x` | Toggle disabled appearance and navigation. |
| `e` | Toggle the empty list. |
| `n` | Preview a narrow layout. |
| `m` | Toggle monochrome. |
| Up/Down or `k`/`j` | Move selection when focused. |
| Home/End, PgUp/PgDn | Navigate or scroll when focused. |
| `q` or Escape | Quit and restore the terminal. |

The details panel disappears below 72 columns. Very small terminals show a quit
hint and wait for resizing. These choices belong to this example, so an
application can use different breakpoints and priorities.

## Select what to load

```zsh
# Paths below assume the repository root is the working directory.
source ./lib/zdraw-ui.zsh     # themes, styles and labels only
source ./lib/zdraw-panel.zsh  # common helpers plus panels
source ./lib/zdraw-list.zsh   # common helpers plus lists and list state updates
source ./lib/zdraw-layout.zsh # common helpers plus pure rectangle calculations
source ./lib/zdraw-table.zsh  # common helpers, layout, selection and tables
source ./lib/zdraw-tabs.zsh   # common helpers plus tabs
source ./lib/zdraw-badge.zsh  # common helpers plus status badges
source ./lib/zdraw-meter.zsh  # common helpers plus numeric progress meters
source ./lib/zdraw-chart.zsh  # headless numeric series and projection
source ./lib/zdraw-sparkline.zsh # chart helpers plus one-row history plots
source ./lib/zdraw-bars.zsh   # chart helpers plus signed bar comparisons
source ./lib/zdraw-canvas.zsh # retained geometry, rasterization and canvas drawing
source ./lib/zdraw-help.zsh   # common helpers plus shortcut rows
source ./lib/zdraw-input.zsh  # editing, selection, paste and field rendering
source ./lib/zdraw-form.zsh   # inputs plus validation and form navigation
source ./lib/zdraw-document.zsh # structured content, wrapping and navigation
source ./lib/zdraw-fixture.zsh  # portable readback fixture export
```

Only source the entries you need. Panel and list loaders include the common
helpers, but do not load one another. Implementation files live under `lib/ui/`;
keep that directory beside the public loaders when distributing the toolkit.

Loading is passive: it defines functions without loading the native module,
initializing curses, reading input, allocating colors or enabling protocols.
Theme resolution and list state updates work without a terminal. Drawing needs
the native `zdraw` module and an initialized session. Input editing and document
compilation use native text queries but do not initialize a terminal.

Each function uses local Zsh option emulation. The loaders disable aliases while
parsing the implementation files. Application options are restored on return.

## Diagnosing failures

For themes, styles, labels, panels, layouts, lists, tables, input, forms,
documents, tabs, meters, badges and help rows, opt into diagnostics with a
descriptor opened by your application **before** entering curses:

```zsh
exec {ZDRAW_UI_DEBUG_FD}>>./zdraw-debug.log
# Run the application, including its normal zdraw end cleanup.
exec {ZDRAW_UI_DEBUG_FD}>&-
unset ZDRAW_UI_DEBUG_FD
```

Failures report the function, invalid utility or missing output parameter, and
status. Native failures include the operation name. Nothing is logged by default;
the helper opens no files, does not close your descriptor and does not use stderr
as a fallback. A closed or malformed sink leaves the original status unchanged.
The diagnostic path uses a subshell to isolate descriptor redirection; successful
calls do not invoke it. Logs can include caller-supplied style values.

This currently covers the shared core and the component families listed above;
chart/canvas, motion and screen/fixture helpers still have validation branches
without diagnostics. It does not capture native stderr or arbitrary application
failures. Input, paste and document diagnostics identify the failed check without
copying the text or payload into the log. Ordinary field-validation outcomes
(such as an empty required field) continue to return their application-facing
message; they are not logged as malformed calls.

Existing return contracts are preserved. Malformed drawing calls generally return
1; layout constraints that do not fit return 2. Validation and action APIs have
their own distinctions. Native failures propagate their status except where the
function already maps failures to its documented API status. For example, `zdraw-input-check` returns 2 for invalid state, including a
failed native text-position query; a diagnostic records both the underlying
failure and the validation status. Ordinary input validation has a distinct
0/1/2 contract. **Status 2 is not a toolkit-wide synonym for insufficient space.** A rectangle outside a
window still returns 1, now with a geometry diagnosis. Branch on the documented
contract of the function you call.

Keep handling failures in the application. Never clear the dirty flag after a
failed render; leave the loop through its cleanup block and report failure once
the terminal is restored. The list/detail recipe already follows that pattern.

## Layout with rectangles

The [layout library](../lib/ui/layout.zsh) computes rectangles without a terminal,
theme or window. Source `lib/zdraw-layout.zsh` and declare `zdraw_ui_layout` as a
writable association and `reply` as a writable array. Coordinates are always
`row column height width`, matching panels and lists.

```zsh
typeset -A zdraw_ui_layout
typeset -a reply body sidebar detail

# A three-row header, flexible body, and two-row footer in a 24x100 region.
zdraw-layout-split 0 0 24 100 rows 0 fixed=3 flex=1 fixed=2 || return
zdraw-layout-rect 2 || return
body=("${reply[@]}")

# A 28-column sidebar, two-column gap, and flexible detail pane.
zdraw-layout-split "${body[@]}" columns 2 fixed=28 flex=1 || return
zdraw-layout-rect 1 || return
sidebar=("${reply[@]}")
zdraw-layout-rect 2 || return
detail=("${reply[@]}")

# Pass either result straight to a component in an initialized session.
zdraw-panel stdscr "${sidebar[@]}" ' Projects ' focus border=rounded
```

`zdraw-layout-split` replaces `zdraw_ui_layout`, so copy any needed rectangles
before making another split. Nested splits can also use a local association in
an application function. Applications decide when to switch layouts or hide a
pane; the library returns geometry without deciding visibility or focus.

```text
zdraw-layout-split row column height width rows|columns gap track [track ...]
zdraw-layout-rect index
zdraw-layout-inset row column height width top right bottom left
zdraw-layout-center row column height width max-height max-width
```

| Operation | Result and behavior |
| --- | --- |
| `split` | Returns `count` and `N,row`, `N,column`, `N,height`, `N,width` fields in `zdraw_ui_layout`, with one-based track indexes. |
| `rect` | Copies a selected track to `reply`, validating its fields. Can read a read-only layout association. |
| `inset` | Returns the inner rectangle in `reply`; inset order is top, right, bottom, left. Excess padding collapses the relevant dimension to zero. |
| `center` | Returns a centered rectangle in `reply`, limited to the requested maximum dimensions and the parent's size. Odd spare cells go to the bottom/right. |

Tracks use `fixed=N` for an exact cell count or `flex=N` for a positive weight.
Fixed tracks and gaps are reserved first. Flexible tracks divide all remaining
cells in proportion to their weights; integer remainders go to earlier flexible
tracks, one cell each. For example, a 12-column split with gap 1 and tracks
`fixed=2 flex=1 flex=2` gives widths 2, 3 and 5. With no flexible track, unused
space remains after the last track.

Gaps exist between every adjacent pair, including zero-sized tracks. Fixed
tracks may be zero; flexible tracks can receive zero space. Empty rectangles
are valid results and compose through further layout operations. Skip component
drawing when either resulting dimension is zero.

For an inset followed by a centered region:

```zsh
zdraw-layout-inset 0 0 24 100 1 2 1 2 || return
zdraw-layout-center "${reply[@]}" 18 72 || return
# reply is now (3 14 18 72).
```

Every integer is checked as decimal text before arithmetic. Values range from
0 to 32,767; each rectangle's exclusive bottom/right edge must also be at most
32,767. A split accepts 1–32 tracks. Layout has no cell-area limit and does not
check window bounds; drawing retains its own stricter limits.

All four calls return zero on success and one for invalid arguments or output
parameters. **A valid split whose fixed tracks and gaps cannot fit returns two.**
Neither failure replaces the previous output. This layout-specific status lets
an application explicitly choose another arrangement. Centering and insetting
clamp to the parent instead of reporting insufficient space. No operation
refreshes, reads input, changes `zdraw_ui_style`, or allocates native resources.

## Compose a panel and list

This draws one frame inside an initialized `stdscr` of at least 12 rows by 54
columns. The application owns `zdraw init`, input and `zdraw end`; see the gallery
for a complete loop with resizing and `always` cleanup.

```zsh
source ./lib/zdraw-panel.zsh
source ./lib/zdraw-list.zsh

typeset -A zdraw_ui_theme zdraw_ui_list
typeset -a reply content projects=('API server' 'Documentation' 'Release notes')
typeset -a project_panel=(border=rounded px=1 py=1 title:fg=accent)

zdraw-ui-theme dark 16 || return
zdraw-panel stdscr 1 2 10 50 ' Projects ' focus "${project_panel[@]}" || return
content=("${reply[@]}")
zdraw-list-update "${#projects}" "$content[3]" keep || return
if (( content[3] && content[4] )); then
  zdraw-list stdscr "${content[@]}" focus \
    selected:bg=4 selected:fg=7 -- "${projects[@]}" || return
fi
zdraw refresh stdscr
```

The panel returns its usable content rectangle. The list consumes that rectangle
and the caller's state. Pass labels as separate, quoted array entries; spaces,
empty entries, wildcard characters and leading hyphens remain data after `--`.

To reuse an appearance, store utilities in an array and append instance choices:

```zsh
typeset -a quiet_panel=(border=ascii border-fg=muted px=1 title:no-bold)
zdraw-panel stdscr 1 2 10 50 Logs inactive \
  "${quiet_panel[@]}" border=double title:fg=accent
```

Nothing registers a global component definition. Those arrays are ordinary
application data and can be passed to custom functions too.

## Themes

```text
zdraw-ui-theme dark|light [auto|rgb|16|256|mono] [role=color ...]
```

Declare a writable association named `zdraw_ui_theme` in the calling scope.
Successful calls replace it atomically. Invalid arguments leave it unchanged.
The profile defaults to `16`, or `mono` when `NO_COLOR` is nonempty. An explicitly
supplied profile wins over `NO_COLOR`. Supply the profile before role overrides.

| Profile | Default palette |
| --- | --- |
| `auto` | Rich dark/light palette, adapted to initialized RGB/256/basic/monochrome capabilities; respects `NO_COLOR`. |
| `rgb` | The same rich palette, requiring initialized native RGB support. |
| `16` | Basic indexed colors 0–7, using the terminal's palette. |
| `256` | Neutral surfaces and a teal/blue accent from the indexed palette. |
| `mono` | Default foreground/background, with selection using reverse video. |

The roles are `canvas`, `surface`, `text`, `muted`, `accent`, `border`,
`selection`, `on-selection`, `inactive`, `on-inactive` and `error`. The association
also records `name` and `profile`. A role's meaning is shared across components;
for example, `selection` is the selected background and `on-selection` is its
foreground.

```zsh
typeset -A zdraw_ui_theme
zdraw-ui-theme dark 256 accent=81 border=243
# Change the entire application on its next redraw:
zdraw-ui-theme light 256 accent=25
```

For easier palette authoring, call `zdraw-ui-theme dark auto` after `zdraw init`.
Hex theme overrides and literal style colors then adapt to the selected output.
See [Colors without palette arithmetic](colors.md) for standalone color
resolution, gradients, ownership, numeric-index encoding and precision limits.
Both new profiles may enable the existing RGB interface and record their
capability snapshot in the theme. The actual profile is reported in `profile`.

Colors accept `default`, decimal indexes 0–255 or `#RRGGBB`. With the original
`16`, `256`, and `mono` profiles, the resolver
canonicalizes indexed values and hex spelling but does not initialize native
color support, query the terminal background or map unavailable colors. The
application chooses a supported profile using `zdraw colorinfo`; the gallery
demonstrates this. RGB requires the existing explicit
[truecolor setup](native-api.md#truecolor). Mixing `default` with an explicit color
requires native default-color support. Monochrome's default/default styles use
pair zero without allocating a color pair.

Reapplying a theme replaces its earlier overrides. Retain custom overrides in an
application array if they should survive a light/dark switch. A `mono` profile
with explicit colored role overrides is allowed; callers who want strictly no
color should omit those overrides.

## Utilities and states

```text
zdraw-ui-style states [utility ...]
```

Declare a writable `zdraw_ui_style` association. Resolution is headless and
atomic. Components resolve into their own local association, preserving a
caller's existing resolved style.

| Utility | Values |
| --- | --- |
| `fg=`, `bg=`, `border-fg=` | A theme role or literal color. |
| `border=` | `none`, `ascii`, `rounded`, `double`. |
| `px=`, `py=` | Horizontal/vertical inset, 0–16 cells on each side. |
| `align=` | `left`, `center`, `right`. |
| `bold`, `underline`, `reverse` | Enable the corresponding attribute. |
| `no-bold`, `no-underline`, `no-reverse` | Disable the corresponding attribute. |

States are comma-separated tags: `normal`, `focus`, `selected`, `inactive`,
`disabled`, `empty`, `title`, `header`, `alternate`, `filled`, `track`, `label`,
`key`, `invalid`, `cursor`, `heading`, `subheading`, `paragraph`, `bullet`,
`quote`, `code`, `separator`, `spacer`, `positive`, `negative`, `axis`, `missing`,
`clipped`. Tags are explicit: `focus` does not implicitly
include `normal`. A utility without a condition applies to every state.

```zsh
typeset -A zdraw_ui_style
zdraw-ui-style selected,focus fg=text bg=surface \
  selected:bg=selection selected:fg=on-selection \
  selected+focus:underline
```

Prefix a utility with `state:` to apply it when that tag is present. Join tags
with `+` for conjunction. There are no implied CSS selectors or specificity:

1. Apply unconditional utilities in argument order.
2. Apply matching conditional utilities in argument order.
3. For the same property within either pass, the last value wins.

Components place their defaults before caller utilities. To override a selected
row's color, use `selected:fg=...`; an unconditional `fg=...` sets the base color
and is followed by the component's selected variant. Likewise use
`title:no-bold` to remove a panel heading's default emphasis. Combined conditions
do not automatically outrank simpler conditions: order within the conditional
pass decides.

Every utility is validated, including variants inactive for the current state.
Misspellings and invalid colors fail instead of remaining hidden until focus
changes. No utility or theme text is evaluated as shell code.

The result contains resolved `fg`, `bg`, `border-fg`, `border`, `px`, `py`,
`align`, and numeric `bold`, `underline`, `reverse` flags. `style` is a complete
native style string; `border-style` carries border foreground and background
without text emphasis. Custom drawing can pass these directly to native calls:

```zsh
zdraw-ui-style normal fg=accent bg=surface bold || return
zdraw spansclip stdscr 0 0 30 "$zdraw_ui_style[style]" 'Build complete'
```

## Component contracts

### Panels

```text
zdraw-panel window row column height width title states [utility ...]
```

Panels fill their rectangle, draw a border and title, and return
`reply=(content-row content-column content-height content-width)`. Declare
`reply` as a writable ordinary array, and copy it before drawing another panel.

Defaults are `fg=text bg=surface border=ascii border-fg=border px=1 py=0`, with
`focus:border-fg=accent` and `title:bold`. The heading resolves the supplied
states plus `title`, allowing different title colors, emphasis and alignment.
Title-only geometry properties do not alter the frame or content rectangle.

Borders consume one cell per edge. Rounded/double borders fall back to ASCII
when the wide drawing path or locale cannot represent them at one-cell width.
A rectangle narrower or shorter than two cells omits its border. Padding can
consume all remaining content; a zero content dimension is a successful result
that callers must skip drawing into. Without a border, a nonempty title consumes
the first available content row. Titles clip to a single line.

### Labels and custom elements

```text
zdraw-label window row column columns text states [utility ...]
```

Labels clear one row, apply horizontal padding and align clipped text. Defaults
are `fg=text bg=surface`. They reject borders and nonzero `py`; compose with a
panel when needed. There is no output parameter. This provides a simple building
block for custom headings, status messages or styled values.

### Lists

```text
zdraw-list-update count visible-rows keep|up|down|home|end|page-up|page-down
zdraw-list window row column height width focus|inactive|disabled \
  [utility ...] [empty-text=text] -- [item ...]
```

Declare `zdraw_ui_list` as a writable association for updates. Its `selected`
and `first` fields are one-based item indexes. `zdraw-list-update` replaces these
fields atomically, clamps selection after data changes and keeps it visible.
Call `keep` after resizing or changing the item count. An empty list has
`selected=0 first=1`; zero visible rows are allowed for state updates. Selection
tracks indexes, so preserving identity across sorting/filtering is application
policy. Application-owned extra fields are retained by list and table updates.

Rendering reads that association without modifying it and never reads keys.
Each item occupies one clipped row. The application maps input to update actions
and decides whether inactive or disabled lists accept interaction. The gallery
only navigates a focused list.

Defaults use `fg=text bg=surface px=0`. Selected rows use `bg=selection`,
`fg=on-selection` and bold text, plus a visible `> ` marker. Inactive selections
use `bg=inactive fg=on-inactive`. Disabled rows use `bg=surface fg=muted` without
bold/reverse; the marker still records selection. Monochrome selections add
reverse video. All these choices can be overridden with state utilities.

The empty state defaults to `No items` with `fg=muted`; change it with
`empty-text=...` and `empty:...` utilities. Lists reject borders and nonzero `py`.
Use the containing panel for those. `align` and `px` apply to a row including its
selection marker; inactive rows reserve the same two marker columns.

### Tables

The [table guide](ui-table.md) covers row-major cell arrays, fixed/flexible
columns, per-column alignment, headers, alternating rows and shared selection
styling. Tables are loaded independently through `lib/zdraw-table.zsh` and
compose with the same panel and layout helpers. The
[table/inspector recipe](recipes/table-inspector.md) demonstrates responsive
columns and a detail pane that follows selection.

### Tabs, badges, meters and help rows

The [presentation guide](ui-presentation.md) documents selectable tab strips,
status badges, progress meters and complete shortcut pairs. These components
have independent loaders and use the same theme and part-state utilities.
The [task-monitor recipe](recipes/task-monitor.md) composes them into Overview
and Queue views with a controllable simulation.

## Ownership, limits and validation

Public output parameters may be global or local in an enclosing function. They
must be declared ordinary writable arrays/associations, without coercion flags.
Read-only theme and list associations may be consumed for drawing. Internal
`_zui_*` names are reserved. Using local output associations is the way to keep
multiple independent themes or lists in separate application functions.

Drawing preserves the native window's cursor and current drawing attributes.
It allocates no windows or prepared rows and performs no refresh, input read,
timeout change or protocol negotiation. The application presents after composing
its frame and remains responsible for terminal cleanup. Native wide-cell repair
can affect the other half of an existing wide glyph crossing a rectangle edge;
avoid overlapping components across such boundaries.

Coordinates and list counts accept unsigned decimal integers up to 32,767,
checked before arithmetic. Rectangles must have positive dimensions, fit inside
the window and contain at most 262,144 cells. Text must satisfy native
`textinfo` rules: printable text with complete base/combining clipping units,
without tabs, newlines, escape sequences or leading combining characters.
Clipping follows native cell widths; it does not introduce full grapheme-cluster
segmentation or guaranteed emoji rendering.

Panel titles and labels are limited to 262,144 characters. A list is limited to
32,767 items and a total of 262,144 characters including the empty message; it
validates offscreen entries too. Theme calls accept at most 32 overrides. Style
resolution accepts at most 128 utilities of 128 characters each, including
component defaults. These are conservative bounds for this first version.

Successful calls return zero. Invalid toolkit arguments return one. Native
failures propagate their status, including unsupported-operation status two.
Pure layout has its separate insufficient-space status described above.
Validation happens before painting for geometry, text and utility arguments.
Painting uses several native calls, so a later native failure, such as exhausted
color pairs, can leave a partially drawn rectangle. It is not a transaction.

Theme changes take effect on redraw and reuse the native color-pair cache.
Discarding a theme does not reclaim pairs. Preview a bounded palette set, monitor
`zdraw colorinfo`, and avoid generating unlimited colors during a session. The
initial renderer validates and resolves on each component draw; applications
should redraw when data, appearance or geometry changes rather than on every
idle input timeout. Large datasets may warrant a future measured optimization.

## Implementation checklist

- [x] Passive, independent component loaders and caller-owned outputs.
- [x] Dark/light themes with basic, 256-color and monochrome profiles.
- [x] Shared utilities, conditional states and reusable argument arrays.
- [x] Panels with customizable borders, titles, padding and empty interiors.
- [x] Labels with cell-aware clipping and alignment.
- [x] Lists with scrolling, focus, inactive, disabled and empty states.
- [x] Interactive gallery with theme, density, border and size exploration.
- [x] Headless validation, retained-cell assertions and PTY interaction tests.
- [x] Pure rectangle splits, weighted tracks, gaps, insets and centering.
- [x] Responsive list/detail recipe preserving selection across pane changes.
- [x] Tables with configurable columns, headers, alignment and row states.
- [x] Responsive table/inspector recipe and empty-data handling.
- [x] Task-monitor recipe with simulated progress, pause, reset and view changes.
- [x] Tabs, status badges, meters and whole-item help rows.
- [x] [Portable visual fixtures and reviewable diffs](visual-regression.md), with theme/density baselines.
- [x] [Editable inputs and forms](inputs-and-forms.md): selection, streaming paste, validation and focus.
- [x] [Semantic documents](semantic-documents.md): word wrapping, role styles, scrolling, named anchors and resize reflow.

This toolkit checklist is complete. Follow-up components are tracked in the
[incremental implementation plan](history/implementation-plan.md); see
[compact charts](compact-charts.md) for sparklines and bar comparisons, and
[character canvas](character-canvas.md) for points, lines and rectangles. The broader experimental ideas in the
research report and native roadmap are historical candidates subject to the
[project stop line](scope.md); they are not scheduled work. Using the toolkit
does not require adopting an application framework or changing the native module.
