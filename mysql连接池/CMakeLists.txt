
# 1. CMake 最低版本
cmake_minimum_required(VERSION 3.10)
project(mysql_conn_pool)

# 2. C++ 版本
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 3. 编译模式 Debug / Release
set(CMAKE_BUILD_TYPE "Debug")

# 4. 编译选项：调试 + 警告 + 多线程
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wall")

# 5. 可执行文件输出到 bin
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 6. 头文件路径
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    /usr/include/mysql
)

# 8. 自动扫描源码
file(GLOB SERVER_SRC ${PROJECT_SOURCE_DIR}/src/*.cpp)
add_executable(mysql_connpool ${SERVER_SRC})


# 10. 链接库文件
target_link_libraries(mysql_connpool  
    mysqlclient
)

