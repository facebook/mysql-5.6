# Copyright (c) 2016 Facebook Inc. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA

INCLUDE_DIRECTORIES(
  ${CMAKE_SOURCE_DIR}/lua
)
SET(LUA_SOURCES  lauxlib.h lcode.h ldblib.c ldo.h lgc.c llex.c lmem.c lobject.h lparser.c lstate.h ltable.c ltm.h lua.h lundump.h lzio.c lapi.c lbaselib.c lcorolib.c ldebug.c ldump.c lgc.h llex.h lmem.h lopcodes.c lparser.h lstring.c ltable.h lua.c lua.hpp lutf8lib.c lzio.h lapi.h lbitlib.c lctype.c ldebug.h lfunc.c linit.c llimits.h loadlib.c lopcodes.h lprefix.h lstring.h ltablib.c luac.c lualib.h lvm.c lauxlib.c lcode.c lctype.h ldo.c lfunc.h liolib.c lmathlib.c lobject.c loslib.c lstate.c lstrlib.c ltm.c luaconf.h lundump.c lvm.h)

ADD_CONVENIENCE_LIBRARY(lua ${LUA_SOURCES})
RESTRICT_SYMBOL_EXPORTS(lua)

IF(MSVC)
   INSTALL_DEBUG_TARGET(lua DESTINATION ${INSTALL_LIBDIR}/debug)
ENDIF()
