# Angle bracket includes
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
project(lualib C)

if(WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

file(GLOB lua_headers
    "lua/*.h"
)

file(GLOB lua_sources
    "lua/*.c"
)

# Create target.
add_library (${PROJECT_NAME} STATIC ${lua_sources})
set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "Dependencies")