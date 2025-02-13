# Copyright (c) 2014-2021 AscEmu Team <http://www.ascemu.org>

if (NOT BUILD_LUAENGINE)
    message("\tLuaEngine was skipped because BUILD_LUAENGINE is unchecked.")
    return()
endif ()

# set up our project name
project(LuaEngine CXX)

# SRCS - contains the source files that compose this project.
set(SRCS
    GameobjectFunctions.cpp
    LUAEngine.cpp
    LuaGlobal.cpp
    LuaHelpers.cpp
    # required for linker
    ../../world/Server/ServerState.cpp
    ../../world/Spell/SpellCastTargets.cpp
    ../../shared/Util.cpp
)

set(HEADERS
    AuraFunctions.h
    FunctionTables.h
    GameobjectFunctions.h
    GlobalFunctions.h
    ItemFunctions.h
    LUAEngine.h
    LUAFunctions.h
    LuaGlobal.h
    LuaHelpers.h
    LuaMacros.h
    LuaSqlApi.h
    PacketFunctions.h
    SpellFunctions.h
    TaxiFunctions.h
    UnitFunctions.h
)

include_directories(
    ${OPENSSL_INCLUDE_DIR}
    ${PCRE_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/dep/recastnavigation/Detour/Include
    ${CMAKE_SOURCE_DIR}/src/collision
    ${CMAKE_SOURCE_DIR}/src/collision/Management
    ${CMAKE_SOURCE_DIR}/src/collision/Maps
    ${CMAKE_SOURCE_DIR}/src/collision/Models
    ${CMAKE_SOURCE_DIR}/dep/g3dlite/include
    ${CMAKE_SOURCE_DIR}/src/shared
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/dep/lualib/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ZLIB_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/dep/utf8cpp
)

add_library(${PROJECT_NAME} SHARED ${SRCS} ${HEADERS})
target_link_libraries(${PROJECT_NAME} world lualib)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${ASCEMU_SCRIPTLIB_PATH} LIBRARY DESTINATION ${ASCEMU_SCRIPTLIB_PATH})

if (USE_PCH)
    target_precompile_headers(${PROJECT_NAME}
    PRIVATE
    LuaGlobal.h
    LUAEngine.h
    LuaHelpers.h
)
endif ()

unset(sources)
unset(headers)
