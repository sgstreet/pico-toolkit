#
# Copyright (C) 2024 Stephen Street
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

include(ExternalProject)

ExternalProject_Add(picolibc
  PREFIX ${CMAKE_CURRENT_BINARY_DIR}
  GIT_REPOSITORY https://github.com/picolibc/picolibc.git
  GIT_TAG main
  CONFIGURE_COMMAND meson setup --cross-file ${CMAKE_CURRENT_SOURCE_DIR}/cross-cortex-m0-none-eabi.txt -Dc_args=-funwind-tables -Dc_args=-mpoke-function-name -Dmultilib=false -Dprefix=${CMAKE_CURRENT_BINARY_DIR} -Dincludedir=arm-none-eabi/include -Dlibdir=arm-none-eabi/lib -Dspecsdir=arm-none-eabi/lib ${CMAKE_CURRENT_BINARY_DIR}/src/picolibc
  BUILD_COMMAND ninja
  INSTALL_COMMAND ninja install
  LOG_CONFIGURE false
  LOG_BUILD false
  LOG_INSTALL false
  
)

set(ENV{PICOLIBC_PATH} ${CMAKE_CURRENT_BINARY_DIR}/arm-none-eabi)
