# Copyright (c) Tzvetan Mikov.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

add_a2_program(eos
  eos.c ${A2TC_INC}/system.h ${A2TC_INC}/system-inc.h
  )

add_a2_program(eos-irc1
  eos-irc1.c ${A2TC_INC}/system2.h ${A2TC_INC}/system2-inc.h
  )