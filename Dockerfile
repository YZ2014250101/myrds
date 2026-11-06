# ==========================================
# 阶段 1：编译阶段 (Builder)
# ==========================================
# 回归最稳的 gcc 环境
FROM gcc:13-bookworm AS builder

# 安装 CMake 和 zlib 开发库
RUN apt-get update && apt-get install -y \
    cmake \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 拷贝源码和配置
COPY CMakeLists.txt .
COPY src/ ./src/

# 配置并编译
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --parallel $(nproc)

# ==========================================
# 阶段 2：运行阶段 (Runner)
# ==========================================
FROM debian:bookworm-slim

# 运行时只需要 zlib 运行库
RUN apt-get update && apt-get install -y \
    zlib1g \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# 把编译好的文件拷过来
COPY --from=builder /app/build/main .

EXPOSE 8080
CMD ["./main"]