find_package(GTest QUIET)
if(GTest_FOUND)
  # 直接链接 ibmcp_collector 静态库：复用其全部源码、include 目录与 IBMCP_HAS_* 编译定义，
  # 测试目标只需编译 test_*.cpp，避免重复维护源码列表与相对路径。
  add_executable(collector_tests EXCLUDE_FROM_ALL
    test_collector.cpp
    test_data_store.cpp
    test_event_emitter.cpp
    test_collector_loop.cpp
    test_timewheel.cpp
    test_io_pool.cpp
    test_driver_factory.cpp
  )
  target_link_libraries(collector_tests PRIVATE
    ibmcp_collector
    GTest::gtest
    GTest::gtest_main
    pthread
  )
  add_test(NAME collector_tests COMMAND collector_tests)
else()
  message(STATUS "GTest not found, skipping collector_tests (install gtest-devel to enable)")
endif()
