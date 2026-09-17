# Changelog

## 0.1.1 — 2026-09-17

- Support native Zsh 5.8 builds: use build-patch context shared by both
  releases and an owned-array conversion independent of newer internal APIs.
- Add separate Zsh 5.8 and 5.9.2 CI jobs, including matching-shell syntax checks,
  with reproducible minimum-version build instructions.
- Fix outside-origin selection drags when motion repeats `PRESSED1`, including
  layout replacement during a held gesture.

Validation: 174 tests passed on each matching Zsh 5.8 and 5.9.2 build, with no
skips. See the [compatibility verification](docs/portability/zsh-5.8.md).

## 0.1.0 — 2026-09-17

First versioned release of the existing zdraw toolkit. Earlier revisions were
identified by Git commits.

- Independent Zsh module derived from `zsh/curses`, with styled cell drawing,
  Unicode text geometry, retained surfaces, structured input and terminal
  lifecycle operations.
- Optional Zsh companions for themes, layouts, panels, lists, tables, forms,
  documents and compact data displays.
- Experimental pane-confined text selection with source-text extraction,
  retained highlighting, explicit drag ownership and content invalidation.
  Includes a two-pane example, automated tests and recorded Kitty observations.
- Fixed validation of the native `mouse delay` argument, allowing applications
  to disable click aggregation for responsive dragging.

The text-selection API and the separately authorized raster experiment remain
provisional. Versioning does not change the [project scope](docs/scope.md) or
promote experimental interfaces to supported APIs.

Validation: 174 tests passed with the matching Zsh 5.9.2 build. The module still
requires a matching Zsh ABI; a release version does not make its binary portable
between unrelated shell builds.
