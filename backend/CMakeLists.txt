cmake_minimum_required(VERSION 3.20)
project(ibmcp_backend CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

set(IBMCP_BUILD_DIR "/home/meng/ibmbuild")

list(APPEND CMAKE_PREFIX_PATH
    "${IBMCP_BUILD_DIR}/drogon/cmake"
    "${IBMCP_BUILD_DIR}/trantor/cmake"
)

set(ENV{PKG_CONFIG_PATH}
    "${IBMCP_BUILD_DIR}/drogon/lib64/pkgconfig:${IBMCP_BUILD_DIR}/libmodbus/lib/pkgconfig:${IBMCP_BUILD_DIR}/open62541/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}"
)

find_package(Drogon CONFIG REQUIRED PATHS "${IBMCP_BUILD_DIR}/drogon/cmake" NO_DEFAULT_PATH)
find_package(OpenSSL REQUIRED)
find_package(ZLIB QUIET)

find_library(PQ_LIBRARY pq)
if(NOT PQ_LIBRARY)
    message(FATAL_ERROR "libpq not found (install libpq-devel / postgresql-devel)")
endif()
find_path(PQ_INCLUDE_DIR libpq-fe.h)
if(NOT PQ_INCLUDE_DIR)
    message(FATAL_ERROR "libpq-fe.h not found (install libpq-devel / postgresql-devel)")
endif()

# hiredis: Drogon 以 BUILD_REDIS=ON 构建时，libdrogon.a 内的 redis 目标文件会
# 引用 hiredis 符号；若 DrogonConfig 未透传该传递依赖，则需在此显式链接。
find_library(HIREDIS_LIBRARY hiredis)
find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h)
if(NOT HIREDIS_LIBRARY)
    message(FATAL_ERROR "libhiredis not found (install hiredis-devel)")
endif()

find_library(ARGON2_LIBRARY argon2)
if(NOT ARGON2_LIBRARY)
    message(WARNING "libargon2 not found: password hash/verify will be unavailable (install argon2-devel)")
endif()

add_subdirectory(collector)

file(GLOB_RECURSE BACKEND_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

add_executable(ibmcp_server ${BACKEND_SOURCES})
target_include_directories(ibmcp_server PRIVATE include ${PQ_INCLUDE_DIR})
target_link_libraries(ibmcp_server PRIVATE
    Drogon::Drogon
    ibmcp_collector
    OpenSSL::SSL OpenSSL::Crypto
    ${PQ_LIBRARY}
    ${HIREDIS_LIBRARY}
)
target_compile_options(ibmcp_server PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(ARGON2_LIBRARY)
    target_compile_definitions(ibmcp_server PRIVATE IBMCP_HAS_ARGON2)
    target_link_libraries(ibmcp_server PRIVATE ${ARGON2_LIBRARY})
endif()
if(ZLIB_FOUND)
    target_link_libraries(ibmcp_server PRIVATE ZLIB::ZLIB)
endif()

install(TARGETS ibmcp_server RUNTIME DESTINATION bin)
install(DIRECTORY migrations DESTINATION .)
install(FILES config.json DESTINATION .)