#!/usr/bin/env zsh
emulate -R zsh
setopt nounset
typeset root=${0:A:h:h}
fail() { print -ru2 -- "FAIL: $*"; exit 1; }
check() { "$@" || fail "$*"; }
reject() { "$@" 2>/dev/null && fail "unexpected success: $*"; return 0; }
alias local='false ALIAS_LEAK'
setopt shwordsplit ksharrays globsubst
source "$root/lib/zdraw-color.zsh" || exit 1
[[ -o shwordsplit && -o ksharrays && -o globsubst ]] || fail 'loader options'
unsetopt shwordsplit ksharrays globsubst
unalias local
zmodload -e zdraw && fail 'loader activated module'
whence -w zdraw-ui-theme >/dev/null && fail 'color companion loaded components'
typeset -A zdraw_color=(profile 256 encoding indexed rgb_min unknown)
typeset REPLY=sentinel
typeset -a reply=(sentinel)
typeset input expected
for input expected in '#000000' 16 '#ffffff' 231 '#808080' 244 '#080808' 232 \
  '#eeeeee' 255 '#ff0000' 196 '#00ff00' 46 '#0000ff' 21 '#5f87af' 67 \
  000 0 081 81 255 255 blue 4 default default; do
  check zdraw-color "$input"
  [[ $REPLY == $expected ]] || fail "$input: $REPLY != $expected"
done
for input in '' '#abc' '#12345g' 256 -1 '1+evil=1' '#$(false)' '1/0'; do
  REPLY=sentinel
  reject zdraw-color "$input"
  [[ $REPLY == sentinel ]] || fail 'failed color changed output'
done
(( ! ${+evil} )) || fail 'arithmetic injection'
check zdraw-color-gradient '#000000' '#ffffff' 5
[[ $reply == '16 238 244 250 231' ]] || fail 'gray gradient'
reply=(sentinel)
reject zdraw-color-gradient '#000000' default 3
reject zdraw-color-gradient '#000000' '#ffffff' 257
reject zdraw-color-gradient '#000000' '#ffffff' 'evil=1'
[[ $reply == sentinel ]] || fail 'failed ramp changed output'
zdraw_color=(profile rgb encoding rgb rgb_min 8)
check zdraw-color '#000001'
[[ $REPLY == '#000008' ]] || fail 'reserved low RGB'
check zdraw-color 196
[[ $REPLY == '#ff0000' ]] || fail 'indexed color on RGB terminal'
check zdraw-color '#AbCDef'
[[ $REPLY == '#abcdef' ]] || fail 'hex canonicalization'
check zdraw-color-gradient '#ff0000' '#0000ff' 3
[[ $reply == '#ff0000 #800080 #0000ff' ]] || fail 'RGB interpolation'
zdraw_color[rgb_min]=0
check zdraw-color '#000000'
[[ $REPLY == '#000000' ]] || fail 'exact black'
check zdraw-color red
[[ $REPLY == '#800000' ]] || fail 'basic index without ANSI reservation'
zdraw_color[profile]=16
check zdraw-color '#ff0000'
[[ $REPLY == '#800000' ]] || fail 'basic fallback without ANSI reservation'
zdraw_color[profile]=256
check zdraw-color '#808080'
[[ $REPLY == '#808080' ]] || fail 'quantized RGB gray encoding'
check zdraw-color '#ff0000'
[[ $REPLY == '#ff0000' ]] || fail 'quantized RGB red encoding'
zdraw_color=(profile 16 encoding indexed rgb_min unknown)
check zdraw-color '#ff0000'
[[ $REPLY == 1 ]] || fail 'basic red'
check zdraw-color 231
[[ $REPLY == 7 ]] || fail 'basic white'
zdraw_color[profile]=mono
check zdraw-color '#ffffff'
[[ $REPLY == default ]] || fail 'monochrome'
reject zdraw-color nonsense
() {
  local -r REPLY=sentinel
  reject zdraw-color '#ffffff'
}
() {
  local -ar reply=(sentinel)
  reject zdraw-color-gradient '#000000' '#ffffff' 2
}
zdraw_color=(profile rgb encoding rgb rgb_min 'evil=1')
reject zdraw-color '#ffffff'
(( ! ${+evil} )) || fail 'metadata arithmetic injection'
print -r -- 'UI COLORS PASS'
