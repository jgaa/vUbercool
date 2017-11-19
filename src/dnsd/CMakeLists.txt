
project(dnsd)
set(SOURCES
    DnsDaemon.cpp
    )

add_library(dnsd ${SOURCES})
add_dependencies(dnsd externalWarlib)
target_include_directories(dnsd
    PRIVATE $<BUILD_INTERFACE:${VUBERCOOL_ROOT}/include> $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_BINARY_DIR}/generated-include/
    PRIVATE ${VUBERCOOL_ROOT}/src/dnsd
    )

set_property(TARGET dnsd PROPERTY CXX_STANDARD 14)
target_compile_definitions(dnsd PUBLIC -DBOOST_COROUTINES_NO_DEPRECATION_WARNING=1)
target_compile_definitions(dnsd PUBLIC -DBOOST_COROUTINE_NO_DEPRECATION_WARNING=1)
target_link_libraries(dnsd
    ${Boost_LIBRARIES}
    warcore
    ${OPENSSL_LIBRARIES}
    )
