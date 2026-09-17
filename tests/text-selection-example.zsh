#!/usr/bin/env zsh
emulate -R zsh
typeset report_fd=$1 control_fd=$2
shift 2
function selection-demo-observe {
  print -r -u "$report_fd" -- "frame $rows $cols $revision ${zdraw_text_selection[active]:-0} ${zdraw_text_selection[selected]:-0} ${zdraw_text_selection[consumed]:-0} $neighbour $modal $enabled ${(qqqq)copied}"
  read -r -u "$control_fd" || return 1
}
print -r -u "$report_fd" -- baseline
read -r -u "$control_fd" || exit 1
source "${0:A:h:h}/examples/text-selection.zsh" "$@" || exit 1
print -r -u "$report_fd" -- done
