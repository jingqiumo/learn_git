# 1. 基础配置
cmake_minimum_required(VERSION 3.10)
project(chat_project)

# 2. C++ 版本与编译选项
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE "Debug")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall -pthread -hiredis")

# 3. 可执行文件输出到 bin 目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 4. 头文件搜索路径
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    /home/hsj/muduo
    /home/hsj/build/release-install-cpp11/include
    /usr/include/mysql
)

# 5. muduo 库文件路径
link_directories(
    /home/hsj/build/release-install-cpp11/lib
)

# 6. 编译服务端程序
file(GLOB SERVER_SRC
    ${PROJECT_SOURCE_DIR}/src/server/*.cpp
)
add_executable(chat_server ${SERVER_SRC})

# 7. 编译客户端程序
file(GLOB CLIENT_SRC
    ${PROJECT_SOURCE_DIR}/src/client/*.cpp
)
add_executable(chat_client ${CLIENT_SRC})

# 8. 链接 muduo 库（服务端和客户端都需要）
target_link_libraries(chat_server
    muduo_net
    muduo_base
    mysqlclient
    pthread
    hiredis
)
target_link_libraries(chat_client
    pthread
)