project(vUberCoolDaemon)

set(ACTUAL_SOURCES
    StatisticsManager.cpp
    VhostManager.cpp
    main.cpp
    )

# set(HEADERS
#     wfded.h
#     TestConfig.h
#     )

set(UBERCOOLD_SOURCES ${ACTUAL_SOURCES})


add_executable(vUbercoold ${UBERCOOLD_SOURCES})

target_link_libraries(vUbercoold ${BOOST_LIBRARIES}
    war_log war_tasks httpd dnsd hosts pthread)
