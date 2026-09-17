# SPDX-License-Identifier: LicenseRef-Zsh
# Zsh licence: ../LICENCE
# Experimental, passive pane-confined selection. See docs/text-selection.md.
() {
  builtin emulate -L zsh
  builtin setopt no_aliases
  builtin source "${1:A:h}/ui/text-selection.zsh"
} "${(%):-%x}"
