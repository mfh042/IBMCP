FROM openeuler/openeuler:24.03-lts

RUN dnf install -y gcc-c++ cmake make git pkgconf-pkg-config \
    openssl-devel libuuid-devel zlib-devel jsoncpp-devel \
    postgresql-devel hiredis-devel argon2-devel \
    tar gzip which file && dnf clean all

# 与 deployments/openeuler/bootstrap.sh 保持一致：Trantor / Drogon 均安装到
# /home/meng/ibmbuild，与 backend/CMakeLists.txt 的 NO_DEFAULT_PATH 查找路径对齐（不走 /usr/local）。
RUN mkdir -p /home/meng/ibmbuild \
    && cd /opt && git clone --branch v1.9.5 --depth 1 https://github.com/drogonframework/drogon.git \
    && cd drogon && git submodule update --init \
    && cmake -S trantor -B build-trantor \
         -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/home/meng/ibmbuild/trantor \
         -DBUILD_SHARED_LIBS=OFF \
    && cmake --build build-trantor -j"$(nproc)" \
    && cmake --install build-trantor \
    && cmake -S . -B build-drogon \
         -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/home/meng/ibmbuild/drogon \
         -DCMAKE_PREFIX_PATH=/home/meng/ibmbuild/trantor \
         -DBUILD_SHARED_LIBS=OFF -DBUILD_CTL=ON -DBUILD_EXAMPLES=OFF -DBUILD_ORM=ON -DBUILD_REDIS=ON \
    && cmake --build build-drogon -j"$(nproc)" \
    && cmake --install build-drogon \
    && rm -rf /opt/drogon

WORKDIR /opt/ibmcp
COPY backend/ ./backend/
COPY backend/collector/ ./backend/collector/

RUN cmake -S backend -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)"

EXPOSE 8000
CMD ["/opt/ibmcp/build/ibmcp_server"]