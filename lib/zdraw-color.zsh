# SPDX-License-Identifier: LicenseRef-Zsh
# Zsh licence: ../LICENCE
# Passive, independently usable color helpers.
() {
  builtin emulate -L zsh
  builtin setopt no_aliases
  builtin source "${1:A:h}/ui/color.zsh"
} "${(%):-%x}"
