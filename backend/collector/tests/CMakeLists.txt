find_package(GTest QUIET)
if(GTest_FOUND)
  add_executable(collector_tests
    test_collector.cpp
    test_data_store.cpp
    test_event_emitter.cpp
    test_collector_loop.cpp
    test_timewheel.cpp
    test_io_pool.cpp
    test_driver_factory.cpp
    ${CMAKE_SOURCE_DIR}/src/store/data_store.cpp
    ${CMAKE_SOURCE_DIR}/src/common/event_emitter.cpp
    ${CMAKE_SOURCE_DIR}/src/scheduler/collector_loop.cpp
    ${CMAKE_SOURCE_DIR}/src/scheduler/sim_value.cpp
    ${CMAKE_SOURCE_DIR}/src/scheduler/timewheel.cpp
    ${CMAKE_SOURCE_DIR}/src/scheduler/io_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/drivers/driver_factory.cpp
    ${CMAKE_SOURCE_DIR}/src/drivers/modbus_address.cpp
    ${CMAKE_SOURCE_DIR}/src/drivers/modbus_tcp_driver.cpp
    ${CMAKE_SOURCE_DIR}/src/drivers/modbus_rtu_driver.cpp
    ${CMAKE_SOURCE_DIR}/src/drivers/mqtt_driver.cpp
    ${CMAKE_SOURCE_DIR}/src/drivers/opcua_driver.cpp
  )
  target_include_directories(collector_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
  )
  target_compile_options(collector_tests PRIVATE -Wall -Wextra -Wno-unused-parameter -pthread)
  target_link_libraries(collector_tests PRIVATE GTest::gtest GTest::gtest_main pthread)
  add_test(NAME collector_tests COMMAND collector_tests)
else()
  message(STATUS "GTest not found, skipping collector_tests (install gtest-devel to enable)")
endif()