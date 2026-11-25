FROM openeuler/openeuler:24.03-lts

RUN dnf install -y gcc-c++ cmake make git pkgconf-pkg-config \
    openssl-devel libuuid-devel zlib-devel jsoncpp-devel \
    postgresql-devel hiredis-devel argon2-devel \
    tar gzip which file && dnf clean all

RUN cd /opt && git clone --branch v1.9.5 --depth 1 https://github.com/drogonframework/drogon.git \
    && cd drogon && git submodule update --init \
    && cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
       -DBUILD_CTL=ON -DBUILD_EXAMPLES=OFF -DBUILD_ORM=ON \
    && cmake --build build -j"$(nproc)" \
    && cmake --install build \
    && rm -rf /opt/drogon

WORKDIR /opt/ibmcp
COPY backend/ ./backend/
COPY backend/collector/ ./backend/collector/

RUN cmake -S backend -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)"

EXPOSE 8000
CMD ["/opt/ibmcp/build/ibmcp_server"]