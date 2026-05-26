cmake_minimum_required(VERSION 3.10)
project(dkv)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -pthread")

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# ============ Protobuf ============
find_package(Protobuf REQUIRED)

set(PROTO_DIR ${PROJECT_SOURCE_DIR}/proto)
set(PROTO_FILES
    ${PROTO_DIR}/common.proto
    ${PROTO_DIR}/kv_rpc.proto
    ${PROTO_DIR}/pd_rpc.proto
    ${PROTO_DIR}/raft_rpc.proto
)

PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# ============ Include paths ============
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/common
    ${PROJECT_SOURCE_DIR}/src/data_node
    ${PROJECT_SOURCE_DIR}/src/pd_server
    ${PROJECT_SOURCE_DIR}/src/client_sdk
    /home/hsj/muduo
    /home/hsj/build/release-install-cpp11/include
)

link_directories(
    /home/hsj/build/release-install-cpp11/lib
)

# ============ Find packages ============
# RocksDB - manual detection (no FindRocksDB.cmake in older versions)
find_path(ROCKSDB_INCLUDE_DIR rocksdb/db.h PATHS /usr/include)
find_library(ROCKSDB_LIBRARY rocksdb PATHS /usr/lib)
if(ROCKSDB_INCLUDE_DIR AND ROCKSDB_LIBRARY)
    message(STATUS "Found RocksDB: ${ROCKSDB_LIBRARY}")
    include_directories(${ROCKSDB_INCLUDE_DIR})
    set(ROCKSDB_TARGET ${ROCKSDB_LIBRARY})
else()
    message(FATAL_ERROR "RocksDB not found. Install: sudo apt install librocksdb-dev")
endif()

# GTest
find_package(GTest)
if(NOT GTEST_FOUND)
    find_path(GTEST_INCLUDE_DIR gtest/gtest.h PATHS /usr/include)
    find_library(GTEST_LIBRARY gtest PATHS /usr/lib/x86_64-linux-gnu /usr/lib)
    find_library(GTEST_MAIN_LIBRARY gtest_main PATHS /usr/lib/x86_64-linux-gnu /usr/lib)
    if(GTEST_INCLUDE_DIR AND GTEST_LIBRARY)
        include_directories(${GTEST_INCLUDE_DIR})
        set(GTEST_LIBRARIES ${GTEST_LIBRARY} ${GTEST_MAIN_LIBRARY} pthread)
    else()
        message(FATAL_ERROR "GTest not found. Install: sudo apt install libgtest-dev")
    endif()
endif()

# ============ Common library ============
set(COMMON_SRC
    src/common/codec.cpp
    src/common/config.cpp
    src/common/shard_router.cpp
    ${PROTO_SRCS}
)
add_library(dkv_common STATIC ${COMMON_SRC})
target_link_libraries(dkv_common
    muduo_net
    muduo_base
    ${PROTOBUF_LIBRARIES}
    pthread
)

# ============ Data Node ============
set(DATA_NODE_SRC
    src/data_node/main.cpp
    src/data_node/data_server.cpp
    src/data_node/kv_service.cpp
    src/data_node/rocks_engine.cpp
    src/data_node/raft_node.cpp
    src/data_node/raft_service.cpp
)
add_executable(data_node ${DATA_NODE_SRC})
target_link_libraries(data_node
    dkv_common
    muduo_net
    muduo_base
    ${PROTOBUF_LIBRARIES}
    ${ROCKSDB_LIBRARY}
    pthread
)

# ============ PD Server ============
set(PD_SERVER_SRC
    src/pd_server/main.cpp
    src/pd_server/pd_server.cpp
    src/pd_server/pd_service.cpp
    src/pd_server/shard_manager.cpp
    ${PROTO_SRCS}
)
add_executable(pd_server ${PD_SERVER_SRC})
target_link_libraries(pd_server
    muduo_net
    muduo_base
    ${PROTOBUF_LIBRARIES}
    pthread
)

# ============ Client SDK ============
set(CLIENT_SDK_SRC
    src/client_sdk/kv_client.cpp
    src/client_sdk/router.cpp
    src/common/config.cpp
    ${PROTO_SRCS}
)
add_library(dkv_client STATIC ${CLIENT_SDK_SRC})
target_link_libraries(dkv_client
    muduo_net
    muduo_base
    ${PROTOBUF_LIBRARIES}
    pthread
)

# ============ Demo CLI ============
add_executable(dkv_cli
    src/client_sdk/demo_cli.cpp
    src/client_sdk/kv_client.cpp
    src/client_sdk/router.cpp
    ${PROTO_SRCS}
)
target_link_libraries(dkv_cli
    ${PROTOBUF_LIBRARIES}
    pthread
)

# ============ Tests ============
enable_testing()

# Unit test: codec
add_executable(test_codec
    tests/unit/test_codec.cpp
    src/common/codec.cpp
    ${PROTO_SRCS}
)
target_link_libraries(test_codec
    muduo_net
    muduo_base
    ${PROTOBUF_LIBRARIES}
    pthread
    GTest::gtest
    GTest::gtest_main
)

# Unit test: shard_router
add_executable(test_shard_router
    tests/unit/test_shard_router.cpp
    src/common/shard_router.cpp
)
target_link_libraries(test_shard_router
    pthread
    GTest::gtest
    GTest::gtest_main
)

# Integration test: single node
add_executable(test_single_node
    tests/integration/test_single_node.cpp
    src/data_node/rocks_engine.cpp
    src/common/codec.cpp
    src/common/config.cpp
    ${PROTO_SRCS}
)
target_link_libraries(test_single_node
    muduo_net
    muduo_base
    ${PROTOBUF_LIBRARIES}
    ${ROCKSDB_LIBRARY}
    pthread
    GTest::gtest
    GTest::gtest_main
)
