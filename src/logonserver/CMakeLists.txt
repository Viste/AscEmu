# Copyright (c) 2014-2020 AscEmu Team <http://www.ascemu.org>

project(logon VERSION 1.0
  DESCRIPTION "AscEmu logon server."
  LANGUAGES CXX
)

include(Auth/CMakeLists.txt)
include(Console/CMakeLists.txt)
include(LogonCommServer/CMakeLists.txt)
include(Server/CMakeLists.txt)

set(SRC_LOGON_PROJECT
  ${CMAKE_CURRENT_SOURCE_DIR}/LogonStdAfx.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/LogonStdAfx.h
)

if(WIN32)
   set(SRC_LOGON_PROJECT ${SRC_LOGON_PROJECT} ${CMAKE_CURRENT_SOURCE_DIR}/resources.rc)
endif()

# Create target.
add_executable(${PROJECT_NAME} 
  ${SRC_AUTH_FILES}
  ${SRC_CONSOLE_FILES}
  ${SRC_LOGON_COMM_SERVER_FILES}
  ${SRC_SERVER_FILES}
  ${SRC_LOGON_PROJECT}
)
set_property(TARGET ${PROJECT_NAME} PROPERTY FOLDER "Daemons")

# Include dependencies.
target_include_directories(${PROJECT_NAME} PUBLIC 
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src/shared
  ${OPENSSL_INCLUDE_DIR}
  ${ZLIB_INCLUDE_DIRS}
)

# Link dependencies.
target_link_libraries(${PROJECT_NAME} PUBLIC
 shared
 ${MYSQL_LIBRARIES}
 ${ZLIB_LIBRARIES}
 ${PCRE_LIBRARIES}
)

# Required for std::filesystem on *nix.
if(NOT WIN32)
  target_link_libraries(${PROJECT_NAME} stdc++fs)
endif()

if(USE_PCH)
  gen_cxx_pch(${PROJECT_NAME} LogonStdAfx.h ${CMAKE_CURRENT_SOURCE_DIR}/LogonStdAfx.cpp)
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/LogonConf.h.in ${CMAKE_CURRENT_SOURCE_DIR}/LogonConf.h)

#Provide make install rules.
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION .)
