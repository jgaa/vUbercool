project(hosts)
set(ACTUAL_SOURCES
    DefaultHost.cpp
    StaticPageHost.cpp
    )
set(HEADERS
    war_tasks.h
    ${WARLIB_ROOT_DIR}/include/tasks/WarPipeline.h
    ${WARLIB_ROOT_DIR}/include/tasks/WarThreadpool.h
    ${WARLIB_ROOT_DIR}/include/war_asio.h
    )

set(SOURCES ${ACTUAL_SOURCES})

add_library(hosts ${SOURCES})
set_target_properties(hosts PROPERTIES DEBUG_OUTPUT_NAME hostsD)
target_link_libraries(hosts ${BOOST_LIBRARIES} war_log)

