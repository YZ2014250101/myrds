# ==========================================
# 阶段 1：编译阶段
# ==========================================
# 换成 alpine 版本的 gcc
FROM alpine:3.19 AS builder

# alpine 需要手动装 cmake 和 zlib 开发库
RUN apk add --no-cache g++ cmake make zlib-dev

WORKDIR /app

COPY CMakeLists.txt .
# Alpine 下 CMake 生成时会默认使用 make，不需要额外参数
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release

COPY src/ ./src/
RUN cmake --build build --parallel $(nproc)

# ==========================================
# 阶段 2：运行阶段
# ==========================================
# 极简的 alpine 运行环境
FROM alpine:3.19

# 安装 zlib 运行库
RUN apk add --no-cache zlib

WORKDIR /app

COPY --from=builder /app/build/main .
# COPY config.json .

EXPOSE 8080
CMD ["./main"]