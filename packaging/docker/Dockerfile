# =============================================================================
# IBMCP 后端容器化构建（备选方案 / 非主路径）
#
# 主路径：openEuler 裸机 g++ 编译（deployments/openeuler/build.sh），后端 + Nginx 不容器化，
#        因需直接访问宿主 libpq 与协议驱动 .so。本文件仅在「后端也要进容器」时使用。
#
# 注意：
#   - 构建上下文为项目根目录（需包含 backend/ 与 deployments/openeuler/build_drivers.sh）：
#       docker build -f packaging/docker/Dockerfile -t ibmcp-server .
#   - 容器内默认按 config.json 的 127.0.0.1 连 PG/Redis/Node-RED，需 --network host 直连宿主，
#     或挂载自定义 config.json 覆盖地址：
#       docker run -d --network host --name ibmcp-api ibmcp-server
# =============================================================================

# -----------------------------------------------------------------------------
# Stage 1: builder — 编译 Drogon/Trantor + 协议驱动 + 后端产物
# -----------------------------------------------------------------------------
FROM openeuler/openeuler:24.03-lts AS builder

# 编译期依赖（与 bootstrap.sh 对齐；autoconf/automake/libtool 供 libmodbus 源码回退构建）
RUN dnf install -y gcc-c++ cmake make git pkgconf-pkg-config curl tar \
    openssl-devel libuuid-devel zlib-devel jsoncpp-devel c-ares-devel \
    postgresql-devel hiredis-devel argon2-devel \
    autoconf automake libtool \
    && dnf clean all

# 与 backend/CMakeLists.txt 硬编码的 IBMCP_BUILD_DIR 保持一致（/home/meng/ibmbuild）
ENV IBMCP_BUILD_DIR=/home/meng/ibmbuild
RUN mkdir -p "$IBMCP_BUILD_DIR" /tmp/ibmbuild-src

# 1) Trantor + Drogon 静态库 —— 参数与 deployments/openeuler/bootstrap.sh 完全一致
RUN set -eux; \
    cd /opt && git clone --branch v1.9.5 --depth 1 https://github.com/drogonframework/drogon.git; \
    cd drogon && git submodule update --init; \
    cmake -S trantor -B build-trantor \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$IBMCP_BUILD_DIR/trantor" \
        -DBUILD_SHARED_LIBS=OFF; \
    cmake --build build-trantor -j"$(nproc)"; \
    cmake --install build-trantor; \
    cmake -S . -B build-drogon \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$IBMCP_BUILD_DIR/drogon" \
        -DCMAKE_PREFIX_PATH="$IBMCP_BUILD_DIR/trantor" \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_CTL=ON -DBUILD_EXAMPLES=OFF -DBUILD_ORM=ON -DBUILD_REDIS=ON; \
    cmake --build build-drogon -j"$(nproc)"; \
    cmake --install build-drogon; \
    rm -rf /opt/drogon

# 2) 协议驱动库 —— 复用项目自身的 build_drivers.sh，与裸机构建口径一致（缺驱动自动退化模拟值）
COPY deployments/openeuler/build_drivers.sh /tmp/build_drivers.sh
RUN bash /tmp/build_drivers.sh

# 3) 编译后端并 install 到 /opt/ibmcp/dist（bin/ibmcp_server + config.json + migrations/）
WORKDIR /opt/ibmcp
COPY backend/ ./backend/
RUN set -eux; \
    . "$IBMCP_BUILD_DIR/env.sh"; \
    cmake -S backend -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/ibmcp/dist; \
    cmake --build build -j"$(nproc)"; \
    cmake --install build; \
    mkdir -p /opt/ibmcp/dist/logs

# -----------------------------------------------------------------------------
# Stage 2: runtime —— 只含运行时 .so 与后端产物
# -----------------------------------------------------------------------------
FROM openeuler/openeuler:24.03-lts AS runtime

# 运行期依赖（Drogon/Trantor 静态链入二进制，无需其 .so；协议驱动为共享库，从 builder 拷贝）
RUN dnf install -y openssl libuuid zlib jsoncpp c-ares \
    postgresql-libs hiredis libargon2 \
    && dnf clean all

# 协议驱动运行时 .so（路径与裸机一致：/home/meng/ibmbuild/*/lib[64]）
COPY --from=builder /home/meng/ibmbuild /home/meng/ibmbuild
RUN find /home/meng/ibmbuild -maxdepth 2 -type d \( -name lib -o -name lib64 \) -print \
        > /etc/ld.so.conf.d/ibmcp-build.conf 2>/dev/null || true; \
    ldconfig

# 后端产物：bin/ibmcp_server + config.json + migrations/
COPY --from=builder /opt/ibmcp/dist /opt/ibmcp/dist
WORKDIR /opt/ibmcp/dist

EXPOSE 8000
CMD ["/opt/ibmcp/dist/bin/ibmcp_server"]