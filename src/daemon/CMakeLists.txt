
set(SOURCES
    StatisticsManager.cpp
    VhostManager.cpp
    main.cpp
    )

# add_executable(vUbercoold ${UBERCOOLD_SOURCES})
#
# target_link_libraries(vUbercoold ${BOOST_LIBRARIES}
#     war_log war_tasks httpd dnsd hosts pthread)

add_executable(vubercoold ${SOURCES})
set_property(TARGET vubercoold PROPERTY CXX_STANDARD 14)
target_link_libraries(vubercoold PRIVATE ${Boost_LIBRARIES} ${ZLIB_LIBRARIES} httpd dnsd hosts)
target_compile_definitions(vubercoold PRIVATE -DBOOST_COROUTINES_NO_DEPRECATION_WARNING=1)
target_compile_definitions(vubercoold PRIVATE -DBOOST_COROUTINE_NO_DEPRECATION_WARNING=1)

target_include_directories(vubercoold
    PRIVATE ${VUBERCOOL_ROOT}/include
    PRIVATE ${CMAKE_BINARY_DIR}/generated-include
)

install(TARGETS vubercoold DESTINATION ${CMAKE_INSTALL_BINDIR})
