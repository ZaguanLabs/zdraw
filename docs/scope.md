# Project scope and stop line

Decision recorded 2026-09-11. This supersedes expansion suggestions in the
exploration roadmap, implementation plan and earlier research.

Read [design-direction.md](design-direction.md) for the maintainer-approved
creative toolkit vision, layered API model, theme/component relationships and
decision rules for future agents. That direction operates within this stop line;
it does not restart automatic feature expansion.

`zdraw` makes it easier to build polished, responsive text-and-cell terminal
interfaces in native Zsh. Its value is a coherent drawing and interaction
contract, good reusable components, and dependable behavior during real use.

## What belongs

| Layer | Responsibility |
| --- | --- |
| Native module | Text and cell geometry, styled drawing, retained windows and pads, explicit presentation, structured input, resource accounting and terminal lifecycle. |
| Optional Zsh libraries | Composable themes, styles, rectangular layouts, panels, lists, tables, forms, documents and compact data displays. Applications select and customize individual pieces. |
| Development tools | Reproducible builds, diagnostics, representative benchmarks, visual comparisons and interaction tests that support those contracts. |
| Applications | Data models, navigation, commands, business logic, asynchronous work and event-loop policy. |

The native module remains independent and portable, with inherited behavior
preserved and useful changes suitable for adaptation to Zsh. Companion libraries
do not require an application framework. Existing charts and character canvases
remain bounded ways to display data; they are not a route into image rendering.

## The hard stop

### Bounded exception: R1/R2 experiment, 2026-09-14

The maintainer explicitly approved proceeding with an experiment in native
depth-tested screen-space triangles and direct half-block cell packing after
reviewing the z3dfx roadblocks and performance assessment. This authorizes only
the bounded experiment described in [raster-experiment.md](raster-experiment.md),
its correctness fixtures and measurements. It does not designate a supported
graphics API, reopen the old roadmap, authorize image protocols or scaled text,
or waive the quality requirements below for a supported addition.

### General boundary

- Remove image conversion, character-mosaic previews and native image-placement
  experiments. No image renderer or image protocol is part of the supported API.
- Cancel scaled-text work. Text uses the documented terminal-cell geometry.
- Do not grow into a multimedia renderer, terminal emulator, general graphics
  engine, application framework or second event-loop framework.
- Stop automatic progression through old checklists. Unchecked entries are
  historical candidates, not authorization or obligations to implement them.
- Pause new feature families and protocol experiments. Work on the existing
  surface is limited to demonstrated defects, usability and visual polish,
  documentation, portability and measured performance problems until an explicit
  new task is agreed.

Previously shipped features other than images are not removed by this decision.
Their existence is also not a commitment to expand them. Existing lifecycle and
fallback guarantees remain obligations.

## Quality required before adding anything

A new proposal must pass every item below before it becomes supported work:

- [ ] Identify a concrete application task and the current user-visible problem.
- [ ] Compare the best relevant existing implementations on that same task.
  Record versions, configurations and representative inputs. Do not claim
  superiority without evidence.
- [ ] Show a result at least as good as the strongest relevant alternative on
  the task's essential outcomes: legibility, interaction, fidelity or speed.
  Being implemented in Zsh is not a substitute for useful output.
- [ ] Demonstrate why this belongs in `zdraw`; prefer composing an existing tool
  when that gives the better result.
- [ ] Verify actual terminal behavior at ordinary and narrow sizes, through
  resize and lifecycle changes, with usable behavior on supported fallbacks.
- [ ] Set a bounded API, ownership model, maintenance cost and stopping point.
- [ ] Obtain an explicit decision to proceed with that bounded addition.

Passing tests establishes correctness within the tested contract. It does not
establish that the feature is worth shipping. If the visible result misses the
quality bar, reject the feature instead of lowering the bar to finish a checklist.

## Why images were removed

The preview reduced screenshots to coarse character pixels, losing readable text
and meaningful color detail. A larger raster and adaptive palette improved
contrast but did not make the result useful. The separate native-placement
experiment also had unresolved repaint, interruption and resume behavior.
Neither justified a supported image feature or continued expansion.

The implementation, converter, examples, dedicated tests, placement fixtures and
capture artifacts have been removed. Their history remains in Git (`187c39b`,
`6241c08`, `0ab8105`). This is a rejected direction, not a deferred image milestone.
