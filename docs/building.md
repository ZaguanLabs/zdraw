# Building and contributing

## Build and test

Prerequisites:

- Zsh 5.8 or later to run the build script, GNU Make, a C compiler, and standard Unix build
  tools (including a POSIX shell, Awk, and Sed).
- Autoconf (including Autoheader), M4, and Patch to regenerate Zsh's configuration
  with the optional drawing function checks.
- Curses development headers and libraries, such as ncurses, and a terminfo
  database containing `xterm-256color` and `vt100` for the tests. The truecolor
  tests also use ncurses `tic -x` to compile private fixtures under `.build/`.
- Python 3.9 or newer and an installed UTF-8 locale for the PTY tests. The drawing
  tests select a UTF-8 locale from `locale -a`; `ZDRAW_TEST_LOCALE` overrides it.
- Curl, Tar, and Xz for the download example below.

From this repository's root, download and extract a public
[Zsh release](https://www.zsh.org/pub/). This Linux example verifies the minimum
supported version, Zsh 5.8, with a matching shell and module:

```sh
mkdir -p .build/downloads .build/sources
curl -fL https://www.zsh.org/pub/old/zsh-5.8.tar.xz \
  -o .build/downloads/zsh-5.8.tar.xz
printf '%s  %s\n' \
  dcc4b54cc5565670a65581760261c163d720991f0d06486da61f8d839b52de27 \
  .build/downloads/zsh-5.8.tar.xz | sha256sum -c -
tar -xJf .build/downloads/zsh-5.8.tar.xz -C .build/sources
export ZSH_BUILD_ROOT="$PWD/.build/sources/zsh-5.8"
export CFLAGS='-O2 -std=gnu17 -Wno-error=implicit-int -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types'
export zsh_cv_sys_tcsetpgrp=yes
make build
make test ZSH_BIN="$PWD/.build/zsh/Src/zsh"
```

The archive's SHA-256 is listed in the publisher's
[old-release checksums](https://www.zsh.org/pub/old/SHA256SUM).
Use `gmake` instead of `make` on systems where GNU Make has that name.

The `CFLAGS` above are for building Zsh 5.8's original source with current GCC
or Clang: use the GNU C17 dialect and leave legacy implicit-declaration and
pointer-conversion diagnostics as warnings. Otherwise old configure probes can
silently misdetect working system facilities or disable dynamic modules. They
do not enable newer Zsh features or alter the source tree. Preserve any additional
compiler flags your platform requires. The `tcsetpgrp` cache answer is explicitly
Linux-specific: Linux supports it, while Zsh 5.8's configure probe needs a
controlling terminal to discover that. On another platform, configure in a
terminal or establish that platform's correct answer; do not copy this override
as a portability claim. The PTY suite exercises terminal ownership/job control.

To test current Zsh 5.9.2 instead, use
`https://www.zsh.org/pub/zsh-5.9.2.tar.xz`, SHA-256
`36fa734374b44783582cec09bcd67822e2f992c779ec1624ab5596df078d2f81`, from the
publisher's [current checksums](https://www.zsh.org/pub/SHA256SUM). Set
`ZSH_BUILD_ROOT` to that extracted tree; the legacy compiler and `tcsetpgrp`
settings are unnecessary. Run `make clean` before switching build sources, or
use a separate checkout for each version. Build first, then use its shell as
`ZSH_BIN` for all syntax checks as well as the native/PTY tests.

The build copies the supplied source tree to `.build/zsh`, adds this module,
applies the build integration patch in `patches/`, and regenerates configuration
using Autoconf and Autoheader. Configured copies are rechecked with their saved
configuration arguments. It uses Zsh's own build rules to build both the shell and
its modules; tests use that matching shell. All build products stay in `.build/`.
No installation, administrator access, or changes to the supplied tree are needed.
The tests create their own pseudo-terminal, so they also run without an
interactive terminal. `make build` builds without running tests.

An extracted release needs no prior configuration. Standard compiler environment
variables such as `CC`, `CPPFLAGS`, `CFLAGS`, and `LDFLAGS` can be set before the
first build, including paths to curses installed in a nonstandard location.
Alternatively, set `ZSH_BUILD_ROOT` to your own configured in-tree Zsh build
using relative source paths. Separate source/build trees are not supported by
the copy-based harness. Run `make clean` before changing sources or configuration;
it removes the working copy and staged module, preserving downloads and extracted
sources.

## Try the module

From the repository root, in a terminal, start the newly built shell:

```sh
.build/zsh/Src/zsh -df
```

Then run these Zsh commands:

```zsh
module_path=("$PWD/.build/modules")
zmodload zdraw
typeset -a size
zdraw geometry size && print -r -- "$size[1] rows, $size[2] columns"
```

The staged module is `.build/modules/zdraw.so` on systems using the `.so`
module suffix; the build uses the suffix selected by Zsh on other systems.
Type `exit` to leave the test shell.

To use the module in an existing Zsh installation, build against sources and
configuration matching that shell's ABI. In a fresh process, prepend the staged
module directory to `module_path` before `zmodload zdraw`. Changing
`module_path` does not replace an already loaded module. To test a particular
matching shell, run `ZSH_TEST_SHELL=/path/to/zsh make test` with `ZSH_BUILD_ROOT`
still set. A binary built for one Zsh configuration or operating system is not
a universal binary.

Zsh 5.8 is the minimum supported source/runtime version; 5.9.2 is also tested.
See the [minimum-version verification record](portability/zsh-5.8.md).
The implementation
uses Zsh's platform configuration and curses abstractions; Linux is currently
verified, while BSD/macOS and alternative curses libraries still need testing.

## Migrating from this project's zcurses module

The module is now loaded with `zmodload zdraw`. Update consumers as follows:

| Previous name | New name |
| --- | --- |
| `zmodload zsh/curses` | `zmodload zdraw` |
| `zcurses ...` | `zdraw ...` |
| `zcurses_features`, `zcurses_colors`, `zcurses_attrs`, `zcurses_keycodes`, `zcurses_windows` | Corresponding `zdraw_*` parameters |
| `ZCURSES_COLORS`, `ZCURSES_COLOR_PAIRS` | `ZDRAW_COLORS`, `ZDRAW_COLOR_PAIRS` |
| `ZCURSES_TEST_LOCALE`, `ZCURSES_MAKE` | `ZDRAW_TEST_LOCALE`, `ZDRAW_MAKE` |

Subcommands, arguments, return statuses and terminal behavior are unchanged.
There are no automatic aliases for the old names. Stock `zsh/curses` remains
separate and retains its `zcurses` builtin and parameters. Only one module
should own an active curses session in a process; end and unload it before
switching modules.

Run `make clean` once when migrating an existing build cache, then rebuild with
`ZSH_BUILD_ROOT` set as above. The staged artifact is now `zdraw.so` (or the
platform's equivalent suffix) at the root of `.build/modules`.

## Source and upstream contribution

- `Src/Modules/`: forked module sources, retaining Zsh's file layout.
- `Doc/Zsh/`: module documentation in Zsh's native format.
- `upstream/`: preserved original sources, checksums and provenance.
- `tests/`: standalone module tests, with no application dependencies.
- `examples/`: standalone Zsh demonstrations of module primitives.
- `patches/`: small changes to Zsh's configuration checks, applied in `.build/`.
- `.build/`: ignored build inputs and outputs.

Run `make -s patch > zdraw.patch` to export an additive integration patch for
the selected Zsh source release. It adds the module sources, build descriptor,
key generator and manual, plus manual registration and optional configuration
checks; it leaves the stock `zsh/curses` sources intact. Apply it with `patch -p1`, then regenerate
`configure` and `config.h.in` with `autoconf` and `autoheader` and rerun configure.
For a feature submission to `zsh/curses`, adapt the relevant changes to its
original names and interface instead of submitting the whole integration patch.
See [provenance](../upstream/README.md) for the baseline's origin. An upstream
submission also needs tests adapted to Zsh's test harness and review against the
maintainers' current tree. This project is not part of the official Zsh distribution.

The original copyright notices and [Zsh licence](../LICENCE) are retained.


## Continuous integration

[The Test workflow](../.github/workflows/test.yml) independently builds pinned,
checksum-verified Zsh 5.8 and 5.9.2 releases and runs `make test` on Ubuntu 24.04
for pushes and pull requests. Syntax checks also use each matching built shell.
The Linux build explicitly uses `--as-needed` to exercise native unload/reload
when libtinfo remains loaded independently of libncurses.
The suite uses the matching built shell, including its PTY tests and native
compile-time variants. It does not run the separate graphical terminal matrix
and does not establish macOS/BSD portability. Those configurations remain unverified.
