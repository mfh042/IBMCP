# IBMCP C++ ²É¼¯À©Õ¹£¨pybind11£©
cmake_minimum_required(VERSION 3.20)
project(ibmcp_collector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(pybind11 CONFIG REQUIRED)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(MODBUS QUIET libmodbus)
    pkg_check_modules(MOSQUITTO QUIET libmosquitto)
    pkg_check_modules(OPEN62541 QUIET open62541)
endif()

set(COLLECTOR_SOURCES
    src/bindings/module.cpp
    src/collector.cpp
    src/scheduler/timewheel.cpp
    src/scheduler/io_pool.cpp
    src/store/data_store.cpp
    src/plan/plan_parser.cpp
    src/plan/plan_validator.cpp
    src/common/event_emitter.cpp
    src/common/logger.cpp
    src/drivers/driver_factory.cpp
)

pybind11_add_module(_core ${COLLECTOR_SOURCES})
target_include_directories(_core PRIVATE include)

if(MODBUS_FOUND)
    target_compile_definitions(_core PRIVATE IBMCP_HAS_MODBUS)
    target_include_directories(_core PRIVATE ${MODBUS_INCLUDE_DIRS})
    target_link_libraries(_core PRIVATE ${MODBUS_LIBRARIES})
endif()
if(MOSQUITTO_FOUND)
    target_compile_definitions(_core PRIVATE IBMCP_HAS_MQTT)
    target_include_directories(_core PRIVATE ${MOSQUITTO_INCLUDE_DIRS})
    target_link_libraries(_core PRIVATE ${MOSQUITTO_LIBRARIES})
endif()
if(OPEN62541_FOUND)
    target_compile_definitions(_core PRIVATE IBMCP_HAS_OPCUA)
    target_include_directories(_core PRIVATE ${OPEN62541_INCLUDE_DIRS})
    target_link_libraries(_core PRIVATE ${OPEN62541_LIBRARIES})
endif()

target_compile_options(_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
install(TARGETS _core LIBRARY DESTINATION industrial_collector)
