# Focus and enhanced keyboard input

Focus reporting and the kitty keyboard subset are opt-in native operations. They
share the existing curses input queue with keys, mouse, paste and capability
replies. Loading the module and ordinary `event` calls enable neither protocol.
Check `focus_events` and `keyboard_events` in `zdraw_features` for compiled APIs;
use [capability evidence](capabilities.md) for terminal observations.

## Focus ownership

```zsh
# After init, send an explicit query and process its capability event:
zdraw query on
zdraw query request focus_events 1000
# In the application's event loop, after an accepted reset report:
zdraw focus on
```

`focus on` requires an accepted mode-1004 report of `reset`. Requiring reset avoids
taking over a mode already enabled by another owner, or one that cannot be
restored. `focus on force` is an explicit application override: it skips that
evidence requirement and assumes responsibility for disabling the mode later.
Neither form changes stored capability evidence.

Events have `type=focus`, `source=focus-report`, `focused=1|0`, and empty `text`.
They do not move application focus between fields or change selection. Applications
choose how terminal focus affects their UI. `focus off` disables the owned mode
and releases newly allocated reply keys. Existing terminfo `kxIN`/`kxOUT` entries
are borrowed when they match the wire sequences and remain intact on cleanup.
Other conflicting definitions fail without replacing them.

Repeated on/off calls are idempotent. While focus owns decoding, use `event`;
legacy `input` is rejected. Suspend disables focus reporting and resume restores
it; end and unload disable and release ownership. No application signal traps
are installed. Failed terminal writes cannot establish what the peer applied.

## Keyboard negotiation and activation

```zsh
zdraw query on
zdraw query request keyboard_events 1000
# Wait in the normal event loop for this capability's reply or timeout.
# After an accepted reply:
zdraw keyboard on
# ... process events ...
zdraw keyboard off
```

The keyboard query sends `CSI ? u`. Known replies advertise current flag values
0–31, recorded as decimal `keyboard_events,reported`. A response of zero confirms
protocol recognition; it does not mean unsupported. Timeout remains unknown.
The query event uses `source=kitty-query`, `mode=0`, and the existing capability
`phase`/`report` fields. Requests share the one-pending-request and one-attempt-per-
capability-per-session policy. Larger future flag combinations are not accepted
as evidence by this version. No terminal-name heuristic is used.

`keyboard on` requires an accepted keyboard query reply; it has no force form.
It pushes flag value **27**: disambiguation, event types, all-key reporting and
associated text. Alternate-layout reporting is not requested. `keyboard off`
pops the saved flag entry. This restores the previous keyboard flags instead of
assuming they were zero. Activation requests these enhancements; it does not
independently confirm each flag on a partially conforming terminal.

An active paste rejects keyboard activation. On/off calls are idempotent.
Successful suspend pops the module's entry; resume pushes a new entry with the
same requested flags. End/unload pop any currently applied entry, including when
a keyboard packet is incomplete. Cleanup never reinstates another protocol after
a disable failure. A failed pop is not retried because a failed flush might still
have reached the peer; terminal I/O failures can leave peer state uncertain.

## Supported key records

Enhanced records have `type=key` and `source=kitty`:

| Field | Contract |
| --- | --- |
| `key` | `U+` codepoint identity, a supported functional name, or `UNKNOWN`. It is separate from typed text. |
| `code`, `code_kind` | Decimal protocol number and `unicode` or `functional`; functional CSI numbers require `key`/`raw` to distinguish encodings. |
| `action` | `press`, `repeat`, or `release`. Missing wire event type defaults to press. |
| `modifiers` | Space-separated `SHIFT ALT CTRL SUPER HYPER META CAPS_LOCK NUM_LOCK`, in that order; empty means none. |
| `modifier_bits` | The decoded 0–255 bitmask. |
| `text`, `encoding` | Associated text encoded as UTF-8, never inferred from the key or Shift; `encoding=utf-8`. |
| `text_status` | `provided` or `none`. Release records must not be treated as text insertion commands. |
| `supported` | `yes` for known identities, `no` for unmapped functional/private-use identities. |
| `shifted_key`, `base_key` | `unknown` in this subset. Alternate fields are bounded and validated but not exposed as layout claims. |
| `raw` | Original bounded wire bytes, Zsh-metafied for safe storage; not safe for direct terminal output. |

The parser accepts CSI-u Unicode identities, plus the CSI letter/tilde encodings
for arrows, Home/End, Insert/Delete, Page Up/Down, F1–F12 and Menu. CSI-u Escape,
Enter, Tab, Backspace and F13–F35 have names. Other private-use identities remain
explicitly unsupported with their numeric identity preserved. Media/keypad/modifier
keys do not gain invented names. Physical-layout mapping, IME composition events
and arbitrary future protocol extensions are outside this subset.

Associated text contains at most 16 Unicode scalar values (64 UTF-8 bytes).
Surrogates and values above U+10FFFF are rejected. NUL/control scalars can occur;
the application still validates text before editing or displaying it. UTF-8 text
is returned consistently even by a narrow curses-input build. Rendering/editing
continues to use the selected locale and existing text API constraints.

Ordinary characters and keys recognized by curses retain their legacy record
shape, even while enhanced input is active. Do not infer repeat/release or exact
modifiers from those records. In particular, common functional presses may use
terminfo sequences, while their richer modifier/release forms use the new parser.
This preserves the existing fallback keymap.

## Parser bounds, ambiguity and failure

Curses recognizes its existing keys, focus reports, paste starts and capability
replies first. A remaining literal Escape followed by `[` enters the CSI-tail
parser on that same queue. A standalone Escape or non-CSI prefix stays on the
legacy path. The parser does not read a second file descriptor or refresh windows;
use `event ... norefresh` to preserve explicit frame presentation.

The tail has a 250 ms monotonic acceptance interval, a 256-byte buffer, and at
most 256 byte reads per call. Empty input preserves partial state between calls;
non-poll continuation waits are capped by the remaining interval. Curses' initial
escape decoding and inherited signal handling still affect total call duration.
A gap before the CSI prefix is recognized can leave input on the legacy path.

Malformed or unsupported packets produce `type=unknown`, `source=kitty`,
`supported=no`, `reason`, and bounded `raw`, with empty `text`, `text_status=none`,
`key=UNKNOWN`, and unknown code/action/modifiers. Oversized packets produce bounded
unknown chunks until a final byte or timeout; their numeric tail is not inserted
as ordinary text while that discard state is active. A new Escape interrupts the
partial packet and is returned to the queue for normal decoding. On timeout,
partial state is released; later bytes cannot reliably be correlated with it.

Suspend and keyboard off reject partial/discard state. Drain it through `event`
(including its timeout) or end the session. End/unload discard the held buffer,
but neither promises to retract bytes still in transit. After ownership is off,
late protocol data may become ordinary input. The module does not authenticate
input or silently flush the keyboard queue.

Paste data takes precedence over keyboard/focus interpretation, and exact query
replies keep their own event type. Resize and mouse records use the existing
schema. Applications should handle event types explicitly and ignore unknown
records rather than forwarding raw terminal bytes into fields.

Activation status is 0 for success, 1 for invalid arguments/state or a library/I/O
failure, and 2 for unavailable compiled support or insufficient activation
evidence. Event calls retain their existing status convention: delivering an
unknown record is success; an incomplete/empty read returns 1 without replacing
the output. Readonly/invalid event targets are rejected before consuming input.

## Examples and verification

```sh
.build/zsh/Src/zsh -df examples/events.zsh --mouse --focus --keyboard
.build/zsh/Src/zsh -df examples/form.zsh --paste --focus --keyboard
```

Both examples request enhancements explicitly, one at a time, and retain the
legacy keymap if negotiation fails. The inspector displays source, action,
modifiers, focus and unsupported-record reasons. The form preserves field
selection when terminal focus changes, ignores release events, uses associated
text for insertion, and supports Ctrl+Shift+S to validate when enhanced input is
active. Enter validation and the legacy editing/navigation keys remain available.

PTY tests cover lifecycle, raw/narrow input, read-target validation, modifiers,
scalar/packet bounds, split and timed-out input, late capability replies, legacy
arrows, mouse, paste, explicit suspend/resume, unload, and balanced push/pop output.
The interactive-shell job-control test also exercises enabled focus through bg/fg.
See [real-terminal results](portability/README.md#enhanced-input-follow-up).

Protocol references: [kitty keyboard protocol](https://sw.kovidgoyal.net/kitty/keyboard-protocol/)
and [xterm focus reporting](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html).

## Held-key inspector (R3)

The existing event API supplies the identities and actions needed for simultaneous
held keys. [The standalone inspector](../examples/held-keys.zsh) demonstrates a
bounded policy without adding state or application controls to the native module:

```sh
.build/zsh/Src/zsh -df examples/held-keys.zsh
.build/zsh/Src/zsh -df examples/held-keys.zsh --legacy
```

The default invocation explicitly requests focus support, then keyboard support.
It enables held state only after both operations succeed, without forcing mode
ownership. Failed negotiation retains legacy input; late replies cannot activate
the held-key policy. `--legacy` skips negotiation entirely. Q or Escape exits;
Ctrl-Z suspends and restores terminal ownership before stopping the process.

The example keeps at most 64 supported `key` identities in a caller-owned Zsh
association. A press adds an identity; release removes it regardless of changed
modifiers or associated text. Repeat never adds an identity. Focus loss, resize,
suspend, unsupported identities and malformed keyboard packets clear the set.
After a clear, a fresh press is required. Presses received while unfocused are
ignored. Ordinary curses characters and arrows retain their legacy semantics
and never acquire inferred release state.

Each input cycle waits for one event, then drains at most 31 additional events
using `poll norefresh`. The inspector redraws only after events or lifecycle
changes, with explicit stage/present. Its 100 ms input timeout and optional 25 ms
curses escape delay are diagnostic defaults, not a frame scheduler or a hard
latency guarantee. Applications choose their own frame timing and apply their
controls from the held set; terminal auto-repeat need not drive movement.

Sourcing the example defines functions only. Its state belongs to the calling
function through Zsh dynamic scope; it is an example, not a new library API.
Session cleanup always clears the set and calls `zdraw end`, including after a
partial packet or signal. Suspend retries a partial packet for a bounded interval
and ends the session if it cannot release ownership.

`tests/test_held_keys.py` verifies simultaneous keys, repeats, modifier changes on
release, focus, resize, suspend/resume, malformed and partial packets, failed/late
negotiation, resource cleanup and terminal settings using PTYs. Child exit and
cleanup waits have deadlines, including a regression check for a child that does
not exit. These tests establish the policy against controlled event streams;
they do not establish a new real-terminal compatibility result. The attempted
additional real-terminal harness was removed after a startup/cleanup hang.
