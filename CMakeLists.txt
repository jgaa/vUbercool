cmake_minimum_required(VERSION 3.0)
project (vubercool VERSION 0.20 LANGUAGES CXX)

set (VUBERCOOL_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

if (NOT DEFINED VUBERCOOL_WITH_TLS)
    option(VUBERCOOL_WITH_TLS "Enable TLS" ON)
endif()

if (NOT DEFINED WITH_APIDOC)
    option(WITH_APIDOC "Generate Doxygen documentation")
endif()

message(STATUS "Using ${CMAKE_CXX_COMPILER}")

include(cmake_scripts/external-projects.cmake)

# Include Boost as an imported target
find_package(Boost 1.50 REQUIRED COMPONENTS
    system
    program_options
    filesystem
    date_time
    context
    coroutine
    chrono
    regex
    iostreams
    )

if (VUBERCOOL_WITH_TLS)
    find_package(OpenSSL REQUIRED)
endif()

find_package(ZLIB REQUIRED)

include_directories(
    ${ZLIB_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
    )

add_subdirectory(src/httpd)
add_subdirectory(src/hosts)
add_subdirectory(src/dnsd)
add_subdirectory(src/daemon)

CONFIGURE_FILE(config.h.template ${CMAKE_BINARY_DIR}/generated-include/vubercool/config.h)

