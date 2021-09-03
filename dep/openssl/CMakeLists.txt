# Copyright (c) 2014-2021 AscEmu Team <http://www.ascemu.org>

set(OPENSSL_EXPECTED_VERSION 1.1.0)

if (NOT DEFINED OPENSSL_ROOT_DIR AND (UNIX OR APPLE))
  set (OPENSSL_ROOT_DIR "/usr/local/opt/openssl" CACHE PATH "default install path" FORCE )
endif()

if (OPENSSL_FOUND)
  # add the include directories for compiling
  target_include_directories(${PROJECT_NAME} PUBLIC ${OPENSSL_INCLUDE_DIR})

  # add the static lib for linking
  target_link_libraries(${PROJECT_NAME} OpenSSL::SSL OpenSSL::Crypto)

  message(STATUS "Found OpenSSL ${OPENSSL_VERSION}")

else()

  message(STATUS "OpenSSL Not Found")

endif()

