cmake_minimum_required(VERSION 3.10.0)
project(kv-store VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 调试模式 + 警告全开，抓UB/隐患
set(CMAKE_BUILD_TYPE Debug)
add_compile_options(-Wall -Wextra -g)

# 头文件目录，让所有源文件可以直接 include "dict.h"
include_directories(src/store)

set(SRC_LIST
    src/main.cpp
    src/store/dict.cpp
    src/store/kv_store.cpp
    src/store/skiplist.cpp
    src/reactor/reactor.cpp
)
add_executable(main ${SRC_LIST})
