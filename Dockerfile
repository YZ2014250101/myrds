# ==========================================
# 阶段 1：编译阶段 (Builder)
# ==========================================
# 使用官方 GCC 镜像，方便直接跑 cmake 和 make
FROM gcc:13-bookworm AS builder

# 安装 CMake 和 zlib 开发库 (因为你的 CMakeLists.txt 里 find_package(ZLIB REQUIRED))
RUN apt-get update && apt-get install -y \
    cmake \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 先拷贝 CMakeLists.txt，单独跑一次 cmake 配置，利用 Docker 缓存
COPY CMakeLists.txt .
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release

# 再拷贝源码
COPY src/ ./src/

# 真正执行编译
RUN cmake --build build --parallel $(nproc)

# ==========================================
# 阶段 2：运行阶段 (Runner)
# ==========================================
# 运行时只需要精简的 Debian 镜像，不需要 gcc 和 cmake
FROM debian:bookworm-slim

# 安装 zlib 运行库（编译时要 zlib1g-dev，运行时只需 zlib1g）
RUN apt-get update && apt-get install -y \
    zlib1g \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 从 builder 阶段把编译好的可执行文件拷过来
COPY --from=builder /app/build/main .

# 把你的配置文件等拷贝过来（如果有的话）
# COPY config.json .

# 假设你的 kv-store 监听 8080 端口（请根据实际代码里的端口修改）
EXPOSE 8079

# 启动命令
CMD ["./main"]