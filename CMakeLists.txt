cmake_minimum_required(VERSION 3.10.0)
project(kv-store VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 头文件目录
include_directories(src/store)

find_package(ZLIB REQUIRED)

set(SRC_LIST
    src/main.cpp
    src/util/util.cpp
    src/protocol/resp_parser.cpp
    src/store/rdb_snapshot.cpp
    src/store/aof_log.cpp
    src/store/dict.cpp
    src/store/kv_store.cpp
    src/store/skiplist.cpp
    src/reactor/reactor.cpp
    src/tcp/tcp_server.cpp
)

# 【模式A：Debug + ASAN，调试找内存bug用，不要跑benchmark】
# set(CMAKE_BUILD_TYPE Debug)
# set(CMAKE_CXX_FLAGS "-g -fsanitize=address -fno-omit-frame-pointer")
# add_compile_options(-Wall -Wextra -g)

# 【模式B：Release O2，性能压测benchmark用！！】
set(CMAKE_BUILD_TYPE Release)

add_executable(main ${SRC_LIST})
target_link_libraries(main PRIVATE ZLIB::ZLIB)
