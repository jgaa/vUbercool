set(SOURCES
    DefaultHost.cpp
    StaticPageHost.cpp
    )

add_library(hosts ${SOURCES})
add_dependencies(hosts externalWarlib)
target_include_directories(hosts
    PRIVATE $<BUILD_INTERFACE:${VUBERCOOL_ROOT}/include> $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_BINARY_DIR}/generated-include/
    PRIVATE ${VUBERCOOL_ROOT}/src/hosts
    )

set_property(TARGET hosts PROPERTY CXX_STANDARD 14)
target_compile_definitions(hosts PUBLIC -DBOOST_COROUTINES_NO_DEPRECATION_WARNING=1)
target_compile_definitions(hosts PUBLIC -DBOOST_COROUTINE_NO_DEPRECATION_WARNING=1)
target_link_libraries(hosts
    ${Boost_LIBRARIES}
    warcore
    ${OPENSSL_LIBRARIES}
    )

