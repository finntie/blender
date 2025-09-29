# SPDX-FileCopyrightText: 2023 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(INC
  .
  ..
  ../editors/include
  ../editors/space_view3d
  ../gpu/intern
  ../makesrna
  ../render/intern
  ../compositor
  ../compositor/algorithms
  ../compositor/cached_resources
  ../compositor/derived_resources
)

set(INC_SYS
)

set(SRC
  network_base.cc
  network_connector.cc

  network_base.hh
  network_connector.hh
)

set(LIB
  PRIVATE bf::blenkernel
  PRIVATE bf::blenlib
  PRIVATE bf::blentranslation
  PRIVATE bf::bmesh
  PRIVATE bf::depsgraph
  PRIVATE bf::dna
  PRIVATE bf::extern::fmtlib
  PRIVATE bf::geometry
  PRIVATE bf::gpu
  PRIVATE bf::intern::guardedalloc
  PRIVATE bf::nodes
  PRIVATE bf::windowmanager
)

if(WITH_UV_SLIM)
  list(APPEND LIB
    bf_intern_slim
  )
  add_definitions(-DWITH_UV_SLIM)
endif()

blender_add_lib(bf_multiplayer "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
