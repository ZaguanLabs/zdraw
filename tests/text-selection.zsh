#!/usr/bin/env zsh
emulate -R zsh
setopt nounset
module_path=("$1")
zmodload zdraw || exit 1
source "${0:A:h:h}/lib/zdraw-text-selection.zsh" || exit 1
fail() { print -ru2 -- "FAIL: $*"; exit 1; }
check() { "$@" || fail "$*"; }
reject() { "$@" 2>/dev/null && fail "unexpected success: $*"; return 0; }
typeset -A zdraw_text_selection zdraw_selection_event
typeset -a reply
typeset REPLY text=$'abcdEF\n  界e\u0301\nlast'
typeset policy=unicode-17.0.0-egc-wcwidth-sum-attach-zero
event() {
  zdraw_selection_event=(type mouse x "$1" y "$2" buttons "$3" modifiers '')
  check zdraw-text-selection-event r1
}
selected() {
  check zdraw-text-selection-get
  [[ $REPLY == "$1" ]] || fail "selection ${(qqq)REPLY} expected ${(qqq)1}"
}
setup() {
  check zdraw-text-selection-init "$text" r1 3 5 6 6 "$policy" \
    0 0 0 bold abcd 1 0 4 underline EF 2 0 7 '' $'  界e\u0301' 3 0 16 '' last
}
setup
# No press outside the text, including blank padding, starts selection.
event 4 3 PRESSED1; event 5 3 ''; event 6 3 RELEASED1
(( ! zdraw_text_selection[selected] && ! zdraw_text_selection[consumed] )) || fail outside
event 10 3 PRESSED1
(( ! zdraw_text_selection[selected] )) || fail padding
event 10 3 RELEASED1
# Repeated pressed states during an outside-origin drag are never new anchors.
event 4 3 PRESSED1; event 6 3 PRESSED1; event 7 4 PRESSED1
(( ! zdraw_text_selection[selected] && ! zdraw_text_selection[consumed] )) || fail 'outside repeated press'
setup
event 6 3 PRESSED1
(( ! zdraw_text_selection[selected] && ! zdraw_text_selection[consumed] )) || fail 'outside replacement'
event 6 3 RELEASED1
event 6 3 PRESSED1
selected b
event 99 4 ''
selected bcdEF
event 99 4 RELEASED1
(( ! zdraw_text_selection[active] && zdraw_text_selection[consumed] )) || fail release
# Reverse drag gives identical source bytes; no soft-wrap newline appears.
event 6 4 PRESSED1; event 6 3 ''; event 6 3 RELEASED1
selected bcdEF
# Every edge/corner stays confined; trailing unused rows mean source end.
typeset -i x y
for x in -10 5 99; do
  for y in -10 3 99; do
    event 6 3 PRESSED1; event $x $y ''; event $x $y RELEASED1
    (( zdraw_text_selection[start] >= 0 && zdraw_text_selection[end] <= 20 && ! zdraw_text_selection[active] )) || fail clamp
  done
done
event 5 3 PRESSED1; event 99 99 ''; event 99 99 RELEASED1
selected "$text"
event 7 5 PRESSED1; event 8 5 RELEASED1
selected 界
event 9 5 PRESSED1; event 9 5 RELEASED1
selected $'e\u0301'
check zdraw-text-selection-spans 2
[[ $reply[-1] == $'e\u0301' ]] || fail spans
# Clear leaves exact original styles available to redraw.
zdraw_selection_event=(type character text $'\e')
check zdraw-text-selection-event r1
(( zdraw_text_selection[consumed] && ! zdraw_text_selection[selected] )) || fail escape
check zdraw-text-selection-spans 0
[[ ${#reply} == 2 && $reply[1] == bold && $reply[2] == abcd ]] || fail styles
# Revision mismatch invalidates, drains the old gesture, and cannot return stale text.
event 5 3 PRESSED1
zdraw_selection_event=(type mouse x 99 y 99 buttons '')
check zdraw-text-selection-event r2
(( ! zdraw_text_selection[valid] && zdraw_text_selection[consumed] )) || fail revision
selected ''
event 99 99 RELEASED1
(( zdraw_text_selection[consumed] && ! zdraw_text_selection[draining] )) || fail drain
setup
event 5 3 PRESSED1
check zdraw-text-selection-invalidate
setup
event 99 99 RELEASED1
selected ''
event 5 3 PRESSED1
check zdraw-text-selection-clear
event 6 3 PRESSED1
(( zdraw_text_selection[consumed] && ! zdraw_text_selection[active] )) || fail 'cancelled repeated button state'
event 6 3 RELEASED1
event 5 3 PRESSED1
check zdraw-text-selection-reset
event 6 3 PRESSED1
selected b
event 6 3 RELEASED1
# Empty pane stays usable. Bad replacement is atomic.
check zdraw-text-selection-init '' r1 0 0 3 4 native
event 0 0 PRESSED1; selected ''; event 0 0 RELEASED1
reject zdraw-text-selection-init abc bad 0 0 3 4 native 0 0 0 '' xyz
[[ $zdraw_text_selection[revision] == r1 ]] || fail atomic
# Split Unicode at row edges must fail, even when each suffix looks printable.
reject zdraw-text-selection-init $'e\u0301' r1 0 0 3 4 "$policy" 0 0 0 '' e
reject zdraw-text-selection-init '👩‍💻' r1 0 0 3 4 "$policy" 0 0 0 '' '👩‍'
# Adjacent style fragments are segmented together; first-scalar style wins.
check zdraw-text-selection-init '👩‍💻X' r1 0 0 2 6 "$policy" \
  0 0 0 bold '👩' 0 0 4 underline '‍💻X'
event 2 0 PRESSED1; event 2 0 RELEASED1
selected '👩‍💻'
check zdraw-text-selection-spans 0
[[ $reply[2] == '👩‍💻' && $reply[3] == underline && $reply[4] == X ]] || fail 'cross-style grapheme'
# Literal spaces belong to the source; display padding does not.
check zdraw-text-selection-init $'a  \n\nb' r1 0 0 4 5 native 0 1 0 '' 'a  ' 1 1 4 '' '' 2 1 5 '' b
event 1 0 PRESSED1; event 99 1 RELEASED1
selected $'a  \n'
print -r -- 'TEXT SELECTION PASS'
