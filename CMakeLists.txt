
cmake_minimum_required(VERSION 2.8)
project (vUbercool)

if (NOT DEFINED VUBERCOOL_ROOT_DIR)
	set(VUBERCOOL_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if (NOT DEFINED WARLIB_ROOT_DIR)
    set(WARLIB_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
endif()

message(STATUS "Using ${CMAKE_CXX_COMPILER}")

add_definitions(-Wall -fPIC -g -o0 -std=c++0x -Wl,--no-as-needed )
#add_definitions(-std=c++0x )

set(LIB_BOOST_PROGRAM_OPTIONS boost_program_options)
set(LIB_BOOST_SERIALIZATION boost_serialization)
set(LIB_BOOST_FILESYSTEM boost_filesystem)
set(LIB_BOOST_DATE_TIME boost_date_time)
set(LIB_BOOST_IOSTREAMS boost_iostreams)
set(LIB_BOOST_SYSTEM boost_system)
set(LIB_BOOST_REGEX boost_regex)
set(LIB_BOOST_CONTEXT boost_context)
set(LIB_BOOST_COROUTINE boost_coroutine)
set(LIB_BOOST_CHRONO boost_chrono)

set (BOOST_LIBRARIES
    ${LIB_BOOST_SYSTEM}
    ${LIB_BOOST_PROGRAM_OPTIONS}
    ${LIB_BOOST_SERIALIZATION}
    ${LIB_BOOST_FILESYSTEM}
    ${LIB_BOOST_DATE_TIME}
    ${LIB_BOOST_IOSTREAMS}
    ${LIB_BOOST_REGEX}
    ${LIB_BOOST_CONTEXT}
    ${LIB_BOOST_COROUTINE}
    ${LIB_BOOST_CHRONO}
    )

set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib CACHE PATH "Destination location")
link_directories(${LIBRARY_OUTPUT_PATH})
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin/${CMAKE_BUILD_TYPE} CACHE PATH "Destination location")

include_directories(
	${PROJECT_SOURCE_DIR}/include
	)

add_subdirectory(src)
#add_subdirectory(tests)

if (WITH_APIDOC)
    find_package(Doxygen)
    if (DOXYGEN_FOUND)

        set(DOXYGEN_INPUT Doxygen)
        set(DOXYGEN_OUTPUT doc/html)

        add_custom_command(
        OUTPUT ${DOXYGEN_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E echo_append "Building API Documentation..."
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_INPUT}
        COMMAND ${CMAKE_COMMAND} -E echo "Done."
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS ${DOXYGEN_INPUT}
        )

        add_custom_target(apidoc ALL DEPENDS ${DOXYGEN_OUTPUT})

    endif (DOXYGEN_FOUND)
endif()

