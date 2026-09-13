# Colors without palette arithmetic

Author colors as `#RRGGBB`, select the available output once, and reuse the
resolved palette. The optional Zsh companion handles RGB, indexed fallbacks and
bounded gradients. Native drawing, presentation and input ownership are unchanged.

Try the interactive example with the matching built shell:

```sh
.build/zsh/Src/zsh -df examples/color-studio.zsh
```

Press `t` for dark/light, `m` for monochrome and `q` to quit. The same palette
drives three ramps, semantic theme roles and a local lavender override. Resize
the terminal to see the layout contract at smaller sizes.

## The shortest component path

Load the UI library and initialize the native session as usual. Then select
`auto` explicitly:

```zsh
source ./lib/zdraw-ui.zsh
typeset -A zdraw_ui_theme
zdraw init || return
{
  zdraw-ui-theme dark auto 'accent=#5edac8' || return
  zdraw-label stdscr 1 2 30 'Shared accent' normal fg=accent || return
  zdraw-label stdscr 2 2 30 'Local lavender' normal 'fg=#bc9cff' || return
  zdraw refresh stdscr
  # The application owns input and decides how long to display the frame.
} always {
  zdraw end
}
```

`auto` supplies a richer dark/light palette, enables the existing native RGB
interface if supported, otherwise selects 256 colors, the eight basic ANSI
colors (the existing `16` profile convention), or monochrome. Nonempty `NO_COLOR`
selects monochrome. The actual selection is in `zdraw_ui_theme[profile]`.
`rgb` selects the same presets but requires RGB support; it returns 2 when
unsupported. Both profiles require an initialized session.

Theme roles and literal `fg=`, `bg=` and `border-fg=` overrides use the same
conversion. Conditional utilities use it too. With an automatic monochrome
theme they resolve to `default`; component selection/emphasis still uses its
existing reverse/bold treatment. Keep text or symbols for status meaning.

The existing default and explicit `16`, `256`, and `mono` theme calls retain
their passive, literal-color behavior and existing palettes. Opt into `auto`
to get conversion. Its additional `color-profile`, `color-encoding`, and
`rgb-min` theme fields hold the resolver snapshot; do not copy only the role
values if you also want literal overrides to adapt. Theme calls replace previous
overrides, as before. Invalid overrides leave the theme and RGB opt-in unchanged.

## Independent native drawing

The color companion does not require the UI library or any components:

```zsh
source ./lib/zdraw-color.zsh
typeset -A zdraw_color
typeset REPLY foreground background
typeset -a reply ramp

# After zdraw init:
zdraw-color-setup || return
zdraw-color '#5edac8' || return
foreground=$REPLY
zdraw-color '#121820' || return
background=$REPLY
zdraw spans stdscr 1 2 "$foreground/$background" 'Ocean' || return

zdraw-color-gradient '#3265d8' '#64f0c2' 32 || return
ramp=("${reply[@]}")
# Retain ramp and reuse its colors in spans, fills, meters or custom charts.
```

| Function | Contract |
| --- | --- |
| `zdraw-color-setup [auto\|rgb\|256\|16\|mono]` | Populate a writable caller-owned `zdraw_color` association from the initialized session. Default `auto`; explicit profiles override `NO_COLOR`. Unsupported explicit profiles return 2. |
| `zdraw-color COLOR` | Write one native color argument to a declared writable scalar `REPLY`. Accept six-digit hex, decimal 0–255, the eight native color names, or `default`. |
| `zdraw-color-gradient START END COUNT` | Write 2–256 resolved colors to a declared writable array `reply`. Interpolate RGB channels, including both endpoints before conversion. `default` is not a gradient endpoint. |

All functions return 0 on success and 1 on invalid arguments/output parameters.
Setup also propagates native failures. Invalid calls preserve their output.
Hex case and leading decimal zeroes are canonicalized. Loading is passive and
does not load the module, initialize curses or change shell options.

`zdraw_color` records `profile`, `encoding` (`indexed` or `rgb`) and `rgb_min`.
It is a capability snapshot, not a global singleton. Local associations work
through Zsh dynamic scope. Conversion and interpolation perform no terminal I/O,
capability queries or pair allocation. Setup queries the existing native
capabilities and may call `zdraw truecolor on`; it does not read input, probe the
terminal, change `TERM`, redefine the palette, present a frame or disable RGB
already used by the application. Call setup again after starting another session.
If you explicitly disable native RGB, resolve a new supported palette before
drawing new RGB arguments. Previously drawn cells follow the native contract.

Compute palettes and ramps during setup or theme changes. Reuse them on every
frame. The helper has no global color cache; repeated spellings reuse the native
pair cache. Gradients are bounded, but repeatedly creating different palettes
still consumes session pair slots. Monitor `zdraw colorinfo` for the native
budget; end releases it. Conversion cannot prevent native allocation failures.

## Precision and fallbacks

- **RGB:** retain the requested color. Direct descriptions can reserve low
  packed values for ANSI colors. Values below `rgb_min` are clamped to that
  minimum: with a minimum of eight, `#000000` becomes near-black `#000008`.
  Use `black`/`0` for the terminal's palette black, or the strict native API
  when a precise value must be accepted or rejected without approximation.
- **256:** choose the nearest squared RGB distance across the conventional
  xterm 6×6×6 cube and the entire 24-step grayscale ramp. Exclude customizable
  slots 0–15 from hex approximation. Explicit numeric indices remain indices
  on indexed terminals.
- **Basic:** retain explicit indices 0–7; approximate other colors to eight
  conventional ANSI hues. Their displayed RGB values depend on terminal
  configuration. The profile name `16` follows the existing theme API; it
  does not promise sixteen distinct shades.
- **Monochrome:** resolve all valid colors to `default`. A foreground/background
  pair of `default/default` uses the existing pair-zero behavior.

On direct-color terminals, decimal values above seven are not generally palette
indices. This companion interprets its numeric inputs as conventional xterm
palette colors and emits hex when needed, including when the requested profile
is `256`. It does not change the native builtin's numeric semantics. Indexed
colors 8–15 converted to RGB use the conventional xterm values; customized
terminal palettes may differ. Entries with no reserved ANSI indices also convert
0–7 to conventional RGB values, including when selecting the basic profile.
The helpers do not query or mutate palette slots.

## Why btop works with the same Kitty environment

Btop emits RGB SGR sequences directly and can convert them to indexed colors;
it does not need ncurses' reported palette count for that output. See its
[color implementation](https://github.com/aristocratos/btop/blob/0999c29dc19d5900daacebe03637271662679ebd/src/btop_theme.cpp)
and the [earlier comparison](btop-review.md). Ncurses obtains color capabilities
from the selected terminal description, as documented in its
[color manual](https://www.invisible-island.net/ncurses/man/curs_color.3x.html).

With a 256-color `xterm-kitty` description, this companion therefore resolves
hex colors to 256 colors even when `COLORTERM=truecolor`. To use the existing
RGB path in an xterm-compatible terminal that supports the installed
`xterm-direct` description:

```sh
TERM=xterm-direct .build/zsh/Src/zsh -df examples/color-studio.zsh
```

This is a process-local choice, not a recommendation to change shell startup
configuration. Exact RGB still needs the matching description and ncurses
extended-color support; see the [native contract](native-api.md#truecolor).
The helper makes palette authoring portable without adding a second renderer.

## Verification

`make test` covers conversion boundaries, cube/grayscale choices, malformed
input, output atomicity, passive loading, RGB encoding of palette indices,
theme and instance overrides, actual emitted RGB/indexed sequences, and
session restart. The studio's interaction test exercises theme/monochrome
switches, ordinary/narrow/tiny sizes, resize recovery and terminal restoration.
These checks establish the conversion and lifecycle contract, not visual parity
with btop's complete dashboard.

The optional private-Xvfb capture tool renders dark, light, narrow and monochrome
frames in real xterm windows and records versions and source hashes:

```sh
python3 scripts/capture-design-study.py --example color-studio \
  --output .build/color-studio-captures
python3 scripts/capture-design-study.py --example color-studio --term xterm-direct \
  --output .build/color-studio-rgb-captures
```

Requires Xvfb, xterm, xwininfo and ImageMagick `import`; these are development
tools, not application dependencies. The RGB run also needs `xterm-direct`.
