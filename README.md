# zdraw

Build text-and-cell terminal interfaces in Zsh: styled panels, scrolling lists
and tables, editable forms, and responsive layouts. Choose the companion
libraries you need and customize their colors, borders, spacing and states.

The native module supplies drawing, Unicode cell measurement, structured input
and terminal lifecycle operations. It is derived from `zsh/curses`, with a
separate `zdraw` builtin; stock `zcurses` stays separate. Applications own their
data, commands and event-loop policy.

**Current focus:** improve the usefulness, usability and reliability of the
existing toolkit. Feature expansion is stopped. Images and scaled text are out
of scope. See the [scope and quality bar](docs/scope.md).

A separately authorized [R1/R2 raster experiment](docs/raster-experiment.md)
evaluates bounded triangle filling and half-block packing. It is experimental;
the general scope stop remains in force.

## Build and test

You need Zsh, GNU Make, a C compiler, Autoconf/Autoheader, M4, Patch, curses
headers/libraries, Python 3.9+, and a UTF-8 locale. The test terminfo database
must include `xterm-256color` and `vt100`; truecolor fixtures need `tic -x`.
Download a public Zsh release and build from the repository root:

```sh
mkdir -p .build/downloads .build/sources
curl -fL https://www.zsh.org/pub/zsh-5.9.2.tar.xz \
  -o .build/downloads/zsh-5.9.2.tar.xz
# Verify against the publisher's SHA256SUM before extracting:
printf '%s  %s\n' \
  36fa734374b44783582cec09bcd67822e2f992c779ec1624ab5596df078d2f81 \
  .build/downloads/zsh-5.9.2.tar.xz | sha256sum -c -
tar -xJf .build/downloads/zsh-5.9.2.tar.xz -C .build/sources
export ZSH_BUILD_ROOT="$PWD/.build/sources/zsh-5.9.2"
make test
```

The build works in `.build/`, preserves the supplied sources and does not install
anything. Tests use the shell built alongside the module. A module built for one
Zsh configuration is **not a universal binary** for other shells or platforms.
Linux is verified; BSD/macOS remain unverified.

See [build options, ABI matching, migration and upstream contributions](docs/building.md)
for the full instructions and publisher checksum link.

## Try the components

Start with [Choose and combine TUI pieces](docs/choose-and-combine.md) to select
a treatment by its purpose, appearance and space requirements. The review
example combines a linked list, status display and change gutter under one theme:

```sh
.build/zsh/Src/zsh -df examples/review-composition.zsh
```

Press `j`/`k` to select a file, Tab to focus its changes, `t` to switch the shared
theme, and `v` to vary only the status treatment. These three pieces are
experimental examples; the guide also points to the existing library components.

Run these with the matching built shell from the repository root:

```sh
.build/zsh/Src/zsh -df examples/gallery.zsh
.build/zsh/Src/zsh -df examples/list-detail.zsh
.build/zsh/Src/zsh -df examples/table-inspector.zsh
.build/zsh/Src/zsh -df examples/form.zsh
```

The gallery shows theme, border and state variations. The list/detail and
inspector recipes demonstrate responsive panes, selection and scrolling. The
form demonstrates editing and validation.

For broader colors, use `zdraw-ui-theme dark auto` after initialization. Author
theme and instance colors as `#RRGGBB`; the helper selects RGB when supported
or converts to the available palette. The standalone color companion also
supplies reusable gradients. See [the color guide](docs/colors.md), or try:

```sh
.build/zsh/Src/zsh -df examples/color-studio.zsh
```

For a small first program, save this as `hello.zsh` in the repository root and
run `.build/zsh/Src/zsh -df hello.zsh`:

```zsh
emulate -R zsh
module_path=("$PWD/.build/modules")
zmodload zdraw || exit 1
source ./lib/zdraw-ui.zsh || exit 1
typeset -A zdraw_ui_theme event
typeset -a size
zdraw-ui-theme dark mono || exit 1
zdraw init || exit 1
{
  zdraw position stdscr size || exit 1
  zdraw-label stdscr 0 0 "$size[6]" 'Hello from zdraw — q to quit' normal bold || exit 1
  zdraw refresh stdscr || exit 1
  while zdraw event stdscr event; do
    [[ $event[type] == character && $event[text] == q ]] && break
  done
} always {
  zdraw end
}
```

This draws a greeting; press `q` to leave. Start with the recipes for a complete
application that handles resize and render failures.

## Guides

| Task | Start here |
| --- | --- |
| Choose reusable treatments and combine their data, themes and layout | [Choose and combine](docs/choose-and-combine.md) |
| Explore three visual directions with real terminal captures | [Design study](docs/design-study.md) |
| Reuse a themed list/detail treatment | [Linked detail prototype](docs/linked-detail.md) |
| Show compact line numbers and added/removed review markers | [Change gutter prototype](docs/change-gutter.md) |
| Show compact working, waiting, done and failed states | [Status display prototype](docs/status-strip.md) |
| Combine selection, change gutters and status under one theme | [Review composition](docs/recipes/review-composition.md) |
| Choose components, themes and style utilities | [UI toolkit](docs/ui-toolkit.md) |
| Debug component failures | [Toolkit diagnostics](docs/ui-toolkit.md#diagnosing-failures) |
| Compose responsive panes | [List/detail recipe](docs/recipes/list-detail.md) |
| Build configurable tables | [Tables](docs/ui-table.md), [inspector recipe](docs/recipes/table-inspector.md) |
| Use tabs, meters, badges and shortcut hints | [Presentation components](docs/ui-presentation.md) |
| Edit and validate text | [Inputs and forms](docs/inputs-and-forms.md) |
| Display structured documents | [Semantic documents](docs/semantic-documents.md) |
| Display compact data | [Charts](docs/compact-charts.md), [character canvas](docs/character-canvas.md) |
| Handle paste, commands and asynchronous work | [Application integration](docs/application-integration.md) |
| Track simultaneous held keys with explicit fallback and cleanup | [Held-key inspector](docs/enhanced-input.md#held-key-inspector-r3) |
| Use native drawing and input operations | [Native API guide](docs/native-api.md), [Zsh module manual](Doc/Zsh/mod_zdraw.yo) |
| Check terminal support | [Capabilities](docs/capabilities.md), [tested configurations](docs/portability/README.md) |
| Verify appearance and performance | [Visual regression](docs/visual-regression.md), [benchmarks](benchmarks/README.md) |

## Licence

Original zdraw contributions use the **Zsh licence**, recorded in [LICENCE](LICENCE).
Source identifiers use `LicenseRef-Zsh` to refer to those exact terms. Upstream
copyright and permission notices are retained. Unicode-derived tables and test
data retain their separate [Unicode licence](tests/unicode/LICENSE.txt).
