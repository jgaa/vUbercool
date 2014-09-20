
project(httpd)
set(ACTUAL_SOURCES
    HttpDaemon.cpp
    ParseHttpRequest.cpp
    HttpRequest.cpp
    )
set(HEADERS
    war_tasks.h
    ${WARLIB_ROOT_DIR}/include/tasks/WarPipeline.h
    ${WARLIB_ROOT_DIR}/include/tasks/WarThreadpool.h
    ${WARLIB_ROOT_DIR}/include/war_asio.h
    )

set(SOURCES ${ACTUAL_SOURCES})

add_library(httpd ${SOURCES})
set_target_properties(httpd PROPERTIES DEBUG_OUTPUT_NAME vUbercool_httpdD)
target_link_libraries(httpd ${BOOST_LIBRARIES} war_log)

