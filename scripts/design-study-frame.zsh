#!/usr/bin/env zsh
# Private capture runner: freeze a completed real frame until the driver reads it.
emulate -R zsh
typeset capture_dir=$1 capture_focus=$2
shift 2
typeset capture_example=design-study
if [[ ${1-} == --example ]]; then
  capture_example=$2
  [[ $capture_example == (design-study|linked-detail|change-gutter|status-strip|review-composition|color-studio) ]] || exit 1
  shift 2
fi
typeset -i capture_finished=0
function zdraw {
  if [[ $1 == event && $capture_finished == 1 ]]; then
    event=(type character text q)
    return 0
  fi
  builtin zdraw "$@" || return
  case $1 in
    init) focus=$capture_focus ;;
    refresh)
      print -r -- ready > "$capture_dir/ready"
      local acknowledgement
      read -r acknowledgement < "$capture_dir/continue" || return
      capture_finished=1 ;;
  esac
  return 0
}
source "${0:A:h:h}/examples/$capture_example.zsh" "$@"
