
project(dnsd)
set(ACTUAL_SOURCES
    DnsDaemon.cpp
    )
set(HEADERS
    war_tasks.h
    ${WARLIB_ROOT_DIR}/include/tasks/WarPipeline.h
    ${WARLIB_ROOT_DIR}/include/tasks/WarThreadpool.h
    ${WARLIB_ROOT_DIR}/include/war_asio.h
    )

set(SOURCES ${ACTUAL_SOURCES})

add_library(dnsd ${SOURCES})
set_target_properties(dnsd PROPERTIES DEBUG_OUTPUT_NAME vUbercool_dnsdD)
target_link_libraries(dnsd ${BOOST_LIBRARIES} war_log)

