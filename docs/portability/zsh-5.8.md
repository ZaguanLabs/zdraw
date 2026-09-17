# Zsh 5.8 compatibility verification

Recorded 2026-09-17, following review of zdraw v0.1.0. Zsh 5.8 is the minimum
supported source/runtime version. Build the module against the shell that will
load it; this does not make modules built for 5.9.2 binary-compatible with 5.8.

## Source and environment

Both builds used public release archives, verified against the publisher's
checksums, in separate build directories:

| Zsh | Archive | SHA-256 |
| --- | --- | --- |
| 5.8 | [Original release](https://www.zsh.org/pub/old/zsh-5.8.tar.xz) | `dcc4b54cc5565670a65581760261c163d720991f0d06486da61f8d839b52de27` |
| 5.9.2 | [Current release](https://www.zsh.org/pub/zsh-5.9.2.tar.xz) | `36fa734374b44783582cec09bcd67822e2f992c779ec1624ab5596df078d2f81` |

Host: Linux x86-64, kernel `6.18.44-desktop-1.mga10`, GCC 15.2.0,
Autoconf 2.72, ncursesw 6.5.20250802, locale `C.UTF-8`.
The matching built executables reported `zsh 5.8 (x86_64-pc-linux-gnu)` and
`zsh 5.9.2 (x86_64-pc-linux-gnu)`.

## Compatibility changes

The v0.1.0 build-integration patch failed both hunks against pristine Zsh 5.8.
Its configure context had changed and its documentation context referenced a
newer module. The replacement patch uses context shared by both releases.
The full exported patch from `make patch` applies to both pristine source trees
with `patch --dry-run --fuzz=0 -p1`.

Zsh's internal `zlinklist2array` takes one argument in 5.8 and two in 5.9.2.
zdraw now performs its own small, permanent, deep-copy conversion before passing
arrays to parameter setters. This preserves ownership semantics without version
macros or newer internal API calls. The stock-curses fallback test also compiles
the selected release's original module, rather than the newer provenance copy.
The preserved files in `upstream/` are unchanged.

With GCC 15, old upstream configure probes need the compiler compatibility flags
in the [build recipe](../building.md). Otherwise probes can silently reject
working facilities and disable dynamic modules. A build without a controlling
terminal also needs the documented Linux-specific `tcsetpgrp` cache answer.
No edits to the supplied Zsh sources were needed: all 1,761 regular files in the
5.8 source tree remained byte-identical to the verified release archive.

## Verification

Each build runs `make test` with `ZSH_BUILD_ROOT` pointing to that release's
original extracted tree and both `ZSH_BIN` and `ZSH_TEST_SHELL` pointing to its
matching built shell. This checks Zsh syntax and exercises the native module,
headless queries, actual PTY input, cleanup/job control, and alternative feature
builds. The text-selection tests include the review's outside-origin repeated
`PRESSED1` regression and a subsequent valid inside-origin gesture.

| Matching shell | Full suite | Duration |
| --- | --- | --- |
| Zsh 5.8 | 174 passed, no failures or skips | 307.110 s |
| Zsh 5.9.2 | 174 passed, no failures or skips | 310.496 s |

The separate syntax checks also passed on each version. Durations are local
test-run observations, not performance guarantees.

Both versions expose the same 48 compiled feature flags with this curses build.
The [CI workflow](../../.github/workflows/test.yml) now defines separate 5.8 and
5.9.2 jobs using pinned archives and matching-shell syntax checks.

This record covers Linux/ncursesw. It does not establish BSD/macOS or alternate
curses compatibility, or repeat the earlier real-emulator screenshot matrix.
