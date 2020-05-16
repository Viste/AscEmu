# Copyright (c) 2014-2020 AscEmu Team <http://www.ascemu.org>

set(PATH_PREFIX Auth)

set(SRC_AUTH_FILES
   ${PATH_PREFIX}/AuthSocket.Legacy.cpp
   ${PATH_PREFIX}/AuthSocket.cpp
   ${PATH_PREFIX}/AuthSocket.h
   ${PATH_PREFIX}/AuthStructs.h
   ${PATH_PREFIX}/AutoPatcher.cpp
   ${PATH_PREFIX}/AutoPatcher.h
)

unset(PATH_PREFIX)
