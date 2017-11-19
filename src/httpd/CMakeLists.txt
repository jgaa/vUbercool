
set(SOURCES
    HttpDaemon.cpp
    ParseHttpRequest.cpp
    HttpRequest.cpp
    )

add_library(httpd ${SOURCES})
add_dependencies(httpd externalWarlib)
target_include_directories(httpd
    PRIVATE $<BUILD_INTERFACE:${VUBERCOOL_ROOT}/include> $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_BINARY_DIR}/generated-include/
    PRIVATE ${VUBERCOOL_ROOT}/src/httpd
    )

set_property(TARGET httpd PROPERTY CXX_STANDARD 14)
target_compile_definitions(httpd PUBLIC -DBOOST_COROUTINES_NO_DEPRECATION_WARNING=1)
target_compile_definitions(httpd PUBLIC -DBOOST_COROUTINE_NO_DEPRECATION_WARNING=1)
target_link_libraries(httpd
    ${Boost_LIBRARIES}
    warcore
    ${OPENSSL_LIBRARIES}
    )
