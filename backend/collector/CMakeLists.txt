# IBMCP 采集器核心（静态库，由上层 ibmcp_server 直接链接进最终二进制）
# 协议驱动库在运行时按需加载（pkg-config 探测 libmodbus/libmosquitto/open62541）

set(COLLECTOR_SOURCES
    src/collector.cpp
    src/scheduler/timewheel.cpp
    src/scheduler/io_pool.cpp
    src/scheduler/collector_loop.cpp
    src/scheduler/sim_value.cpp
    src/store/data_store.cpp
    src/plan/plan_parser.cpp
    src/plan/plan_validator.cpp
    src/common/event_emitter.cpp
    src/common/logger.cpp
    src/drivers/driver_factory.cpp
    src/drivers/modbus_address.cpp
    src/drivers/modbus_tcp_driver.cpp
    src/drivers/modbus_rtu_driver.cpp
    src/drivers/mqtt_driver.cpp
    src/drivers/opcua_driver.cpp
)

add_library(ibmcp_collector STATIC ${COLLECTOR_SOURCES})
set_target_properties(ibmcp_collector PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(ibmcp_collector PUBLIC include src third_party)
target_compile_options(ibmcp_collector PRIVATE -Wall -Wextra -Wno-unused-parameter)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(MODBUS QUIET libmodbus)
    pkg_check_modules(MOSQUITTO QUIET libmosquitto)
    pkg_check_modules(OPEN62541 QUIET open62541)
endif()

if(MODBUS_FOUND)
    target_compile_definitions(ibmcp_collector PUBLIC IBMCP_HAS_MODBUS)
    target_include_directories(ibmcp_collector PUBLIC ${MODBUS_INCLUDE_DIRS} ${MODBUS_INCLUDEDIR})
    target_link_directories(ibmcp_collector PUBLIC ${MODBUS_LIBRARY_DIRS})
    target_link_libraries(ibmcp_collector PUBLIC ${MODBUS_LIBRARIES})
endif()
if(MOSQUITTO_FOUND)
    target_compile_definitions(ibmcp_collector PUBLIC IBMCP_HAS_MQTT)
    target_include_directories(ibmcp_collector PUBLIC ${MOSQUITTO_INCLUDE_DIRS})
    target_link_libraries(ibmcp_collector PUBLIC ${MOSQUITTO_LIBRARIES})
endif()
if(OPEN62541_FOUND)
    target_compile_definitions(ibmcp_collector PUBLIC IBMCP_HAS_OPCUA)
    target_include_directories(ibmcp_collector PUBLIC ${OPEN62541_INCLUDE_DIRS})
    target_link_directories(ibmcp_collector PUBLIC ${OPEN62541_LIBRARY_DIRS})
    target_link_libraries(ibmcp_collector PUBLIC ${OPEN62541_LIBRARIES})
endif()