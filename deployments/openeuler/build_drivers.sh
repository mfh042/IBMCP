#!/usr/bin/env bash
# =============================================================================
# IBMCP 协议驱动库编译安装脚本（openEuler 侧）
#
# 所有产物统一落到 $IBMCP_BUILD_DIR（默认 /home/meng/ibmbuild）下的「每库一子目录」，
# 与 backend/CMakeLists.txt 中硬编码的 PKG_CONFIG_PATH 完全对齐：
#   $IBMCP_BUILD_DIR/libmodbus   → lib/pkgconfig/libmodbus.pc
#   $IBMCP_BUILD_DIR/open62541   → lib/pkgconfig/open62541.pc
#   mosquitto                     → 优先 dnf 装到系统（无独立前缀）
#
# 完成后生成 $IBMCP_BUILD_DIR/env.sh（聚合 PKG_CONFIG_PATH / LD_LIBRARY_PATH / PATH）
# 并把各 lib 目录注册进 /etc/ld.so.conf.d/ibmcp-build.conf + ldconfig，
# 使「干净 shell / systemd 非登录 shell」都能找到运行时 .so。
#
# 用法（需 root 或 sudo）：
#   sudo bash deployments/openeuler/build_drivers.sh
# 可覆盖变量：
#   IBMCP_BUILD_DIR      安装根，默认 /home/meng/ibmbuild
#   LIBMODBUS_VERSION    默认 3.2.0
#   OPEN62541_VERSION    默认 1.4.5
#   IBMCP_GITHUB_MIRROR  GitHub 加速前缀（如 https://kkgithub.com），留空用直连
# =============================================================================
set -euo pipefail

IBMCP_BUILD_DIR="${IBMCP_BUILD_DIR:-/home/meng/ibmbuild}"
IBMCP_DRIVER_SRC="${IBMCP_DRIVER_SRC:-/tmp/ibmbuild-src}"
LIBMODBUS_VERSION="${LIBMODBUS_VERSION:-3.2.0}"
OPEN62541_VERSION="${OPEN62541_VERSION:-1.4.5}"
IBMCP_GITHUB_MIRROR="${IBMCP_GITHUB_MIRROR:-https://kkgithub.com}"

log()  { echo "[build_drivers] $*"; }
warn() { echo "[build_drivers] ⚠️  $*"; }
die()  { echo "[build_drivers] ❌ $*"; exit 1; }

if [ "$(uname -s)" != "Linux" ]; then
    die "此脚本只能在 Linux (openEuler) 上运行"
fi

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    command -v sudo >/dev/null || die "需要 root 权限：请用 root 或安装 sudo 后执行"
    SUDO="sudo"
fi

for cmd in gcc g++ cmake make pkg-config curl tar; do
    command -v "$cmd" >/dev/null || die "缺少工具 $cmd，请先执行 bootstrap.sh"
done

mkdir -p "$IBMCP_BUILD_DIR" "$IBMCP_DRIVER_SRC"

# ---------------------------------------------------------------------------
# 多镜像下载：依次尝试，全部失败才返回非 0
# ---------------------------------------------------------------------------
download() {
    local out="$1"; shift
    local urls=("$@")
    rm -f "$out"
    for url in "${urls[@]}"; do
        log "  尝试下载：$url"
        if curl -fsSL --connect-timeout 15 --max-time 600 -o "$out" "$url"; then
            return 0
        fi
        rm -f "$out"
    done
    return 1
}
gh_urls() {
    local raw="$1"
    if [ -n "$IBMCP_GITHUB_MIRROR" ]; then
        echo "${IBMCP_GITHUB_MIRROR}/${raw#https://github.com/}"
    fi
    echo "$raw"
}

# ---------------------------------------------------------------------------
# 1. mosquitto：优先 dnf 装（系统路径，无独立前缀）
# ---------------------------------------------------------------------------
install_mosquitto() {
    if pkg-config --exists libmosquitto 2>/dev/null; then
        log "✅ mosquitto 已就绪：$(pkg-config --modversion libmosquitto)"
        return 0
    fi
    if $SUDO dnf install -y mosquitto-devel >/dev/null 2>&1 \
        && pkg-config --exists libmosquitto 2>/dev/null; then
        log "✅ mosquitto-devel 已从 dnf 安装"
        return 0
    fi
    warn "mosquitto-devel 不可用（请确认 EPOL 源已启用），MQTT 驱动将退化模拟值"
    return 1
}

# ---------------------------------------------------------------------------
# 2. libmodbus → $IBMCP_BUILD_DIR/libmodbus
# ---------------------------------------------------------------------------
install_libmodbus() {
    local PREFIX="$IBMCP_BUILD_DIR/libmodbus"
    export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    if pkg-config --exists libmodbus 2>/dev/null; then
        log "✅ libmodbus 已就绪：$(pkg-config --modversion libmodbus @ $PREFIX)"
        return 0
    fi
    if $SUDO dnf install -y libmodbus-devel >/dev/null 2>&1 \
        && pkg-config --exists libmodbus 2>/dev/null; then
        log "✅ libmodbus-devel 已从 dnf 安装（系统路径）"
        return 0
    fi

    local raw_url="https://github.com/stephane/libmodbus/releases/download/v${LIBMODBUS_VERSION}/libmodbus-${LIBMODBUS_VERSION}.tar.gz"
    local tarball="$IBMCP_DRIVER_SRC/libmodbus-${LIBMODBUS_VERSION}.tar.gz"
    local srcdir="$IBMCP_DRIVER_SRC/libmodbus-${LIBMODBUS_VERSION}"

    log "编译 libmodbus ${LIBMODBUS_VERSION} → $PREFIX ..."
    if ! download "$tarball" $(gh_urls "$raw_url"); then
        warn "libmodbus 下载失败，Modbus TCP/RTU 驱动将退化模拟值"; return 1
    fi
    rm -rf "$srcdir"; mkdir -p "$srcdir"
    tar -xzf "$tarball" -C "$srcdir" --strip-components=1
    if [ ! -f "$srcdir/configure" ]; then
        log "  tarball 不含 configure，安装 autotools 并执行 autogen.sh ..."
        $SUDO dnf install -y autoconf automake libtool >/dev/null 2>&1 || true
        (cd "$srcdir" && ./autogen.sh) || { warn "libmodbus autogen.sh 失败"; return 1; }
    fi
    (cd "$srcdir" \
        && ./configure --prefix="$PREFIX" --disable-static \
        && make -j"$(nproc)" \
        && $SUDO make install) \
        || { warn "libmodbus 编译/安装失败"; return 1; }
    log "✅ libmodbus ${LIBMODBUS_VERSION} 已安装到 $PREFIX"
    return 0
}

# ---------------------------------------------------------------------------
# 3. open62541 → $IBMCP_BUILD_DIR/open62541
# ---------------------------------------------------------------------------
install_open62541() {
    local PREFIX="$IBMCP_BUILD_DIR/open62541"
    export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    if pkg-config --exists open62541 2>/dev/null; then
        log "✅ open62541 已就绪：$(pkg-config --modversion open62541 @ $PREFIX)"
        return 0
    fi
    if $SUDO dnf install -y open62541-devel >/dev/null 2>&1 \
        && pkg-config --exists open62541 2>/dev/null; then
        log "✅ open62541-devel 已从 dnf 安装（系统路径）"
        return 0
    fi

    local raw_url="https://github.com/open62541/open62541/archive/refs/tags/v${OPEN62541_VERSION}.tar.gz"
    local tarball="$IBMCP_DRIVER_SRC/open62541-${OPEN62541_VERSION}.tar.gz"
    local srcdir="$IBMCP_DRIVER_SRC/open62541-${OPEN62541_VERSION}"
    local builddir="$srcdir/build"

    log "编译 open62541 ${OPEN62541_VERSION} → $PREFIX ..."
    if ! download "$tarball" $(gh_urls "$raw_url"); then
        warn "open62541 下载失败，OPC UA 驱动将退化模拟值"; return 1
    fi
    rm -rf "$srcdir"; mkdir -p "$srcdir"
    tar -xzf "$tarball" -C "$srcdir" --strip-components=1
    cmake -S "$srcdir" -B "$builddir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DBUILD_SHARED_LIBS=ON \
        -DUA_BUILD_EXAMPLES=OFF \
        -DUA_BUILD_UNIT_TESTS=OFF \
        -DUA_ENABLE_ENCRYPTION=OFF \
        >/dev/null \
        || { warn "open62541 cmake 配置失败"; return 1; }
    cmake --build "$builddir" -j"$(nproc)" >/dev/null \
        || { warn "open62541 编译失败"; return 1; }
    $SUDO cmake --install "$builddir" >/dev/null \
        || { warn "open62541 安装失败"; return 1; }
    log "✅ open62541 ${OPEN62541_VERSION} 已安装到 $PREFIX"
    return 0
}

# ---------------------------------------------------------------------------
# 4. 生成 env.sh 并注册 ldconfig（幂等，扫描 $IBMCP_BUILD_DIR 下所有 */lib* / */bin）
# ---------------------------------------------------------------------------
write_env_and_ldconfig() {
    local pc_dirs=() lib_dirs=() bin_dirs=() d
    shopt -s nullglob
    for d in "$IBMCP_BUILD_DIR"/*/lib/pkgconfig "$IBMCP_BUILD_DIR"/*/lib64/pkgconfig; do
        [ -d "$d" ] && pc_dirs+=("$d")
    done
    for d in "$IBMCP_BUILD_DIR"/*/lib "$IBMCP_BUILD_DIR"/*/lib64; do
        [ -d "$d" ] && lib_dirs+=("$d")
    done
    for d in "$IBMCP_BUILD_DIR"/*/bin; do
        [ -d "$d" ] && bin_dirs+=("$d")
    done
    shopt -u nullglob

    local pc_path="" lib_path="" bin_path=""
    [ ${#pc_dirs[@]}  -gt 0 ] && pc_path="$(IFS=:;  echo "${pc_dirs[*]}")"
    [ ${#lib_dirs[@]} -gt 0 ] && lib_path="$(IFS=:; echo "${lib_dirs[*]}")"
    [ ${#bin_dirs[@]} -gt 0 ] && bin_path="$(IFS=:; echo "${bin_dirs[*]}")"

    cat > "$IBMCP_BUILD_DIR/env.sh" <<EOF
# 由 build_drivers.sh / bootstrap.sh 自动生成，请勿手改
export IBMCP_BUILD_DIR="$IBMCP_BUILD_DIR"
export PKG_CONFIG_PATH="$pc_path\${PKG_CONFIG_PATH:+:\$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$lib_path\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
export PATH="$bin_path\${PATH:+:\$PATH}"
EOF
    chmod 644 "$IBMCP_BUILD_DIR/env.sh"
    log "  已生成 $IBMCP_BUILD_DIR/env.sh"

    if [ ${#lib_dirs[@]} -gt 0 ]; then
        local conf
        conf="$(printf '%s\n' "${lib_dirs[@]}")"
        $SUDO bash -c "cat > /etc/ld.so.conf.d/ibmcp-build.conf <<EOF
$conf
EOF"
        $SUDO ldconfig
        log "  已注册 ldconfig（/etc/ld.so.conf.d/ibmcp-build.conf）"
    fi
}

# =============================================================================
# 执行安装（失败退化，不阻断）
# =============================================================================
install_mosquitto  || true
install_libmodbus  || true
install_open62541  || true

write_env_and_ldconfig

# =============================================================================
# 汇总
# =============================================================================
log "===== 协议驱动库就绪状态 ====="
# shellcheck disable=SC1090
source "$IBMCP_BUILD_DIR/env.sh" 2>/dev/null || true
for p in libmodbus libmosquitto open62541; do
    if pkg-config --exists "$p" 2>/dev/null; then
        log "  ✅ $p $(pkg-config --modversion "$p")  libdir=$(pkg-config --variable=libdir "$p")"
    else
        warn "  ❌ $p 未就绪（对应协议将退化模拟值，不阻断构建）"
    fi
done
log "完成。之后运行：bash deployments/openeuler/build.sh"
