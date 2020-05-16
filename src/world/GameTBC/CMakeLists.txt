# Copyright (c) 2014-2020 AscEmu Team <http://www.ascemu.org>

include(GameTBC/Network/CMakeLists.txt)
include(GameTBC/Storage/CMakeLists.txt)

set(PATH_PREFIX GameTBC)

set(SRC_GAMEBC_FILES
    ${PATH_PREFIX}/UpdateFields.h
)

unset(PATH_PREFIX)
