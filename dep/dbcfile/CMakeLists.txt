# Copyright (c) 2014-2020 AscEmu Team <http://www.ascemu.org>

# set up our project name
project(dbcfile CXX)

set(sources
   dbcfile.cpp
)

include_directories(
   ${CMAKE_SOURCE_DIR}/dep/loadlib
   ${CMAKE_SOURCE_DIR}/dep/libmpq/libmpq
   ${CMAKE_CURRENT_SOURCE_DIR}
)

# Create target.
add_library(${PROJECT_NAME} STATIC ${sources})
set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "Dependencies")

target_link_libraries( ${PROJECT_NAME} loadlib libmpq)

unset(sources)