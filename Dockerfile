FROM alpine:3.19 AS builder

RUN sed -i 's/dl-cdn.alpinelinux.org/mirrors.aliyun.com/g' /etc/apk/repositories
RUN apk add --no-cache g++ cmake make zlib-dev

WORKDIR /app

# 1. 先把源码和 CMakeLists.txt 一起拷进来
COPY CMakeLists.txt .
COPY src/ ./src/

# 2. 然后执行配置
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release

# 3. 最后执行编译
RUN cmake --build build --parallel $(nproc)