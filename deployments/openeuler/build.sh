#!/usr/bin/env bash
# =============================================================================
# IBMCP 后端构建脚本（openEuler 侧）
# 产物：$PROJECT_ROOT/dist/bin/ibmcp_server（含采集器静态库 + Drogon 静态库）
# 依赖：$IBMCP_BUILD_DIR（默认 /home/meng/ibmbuild）下的 drogon/trantor/驱动库
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

IBMCP_BUILD_DIR="${IBMCP_BUILD_DIR:-/home/meng/ibmbuild}"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/backend/build}"
DIST_DIR="${DIST_DIR:-$PROJECT_ROOT/dist}"
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
MAKE_JOBS="${MAKE_JOBS:-$(nproc)}"

echo "============================================================"
echo " IBMCP C++ backend build (Drogon + static collector)"
echo "  PROJECT_ROOT = $PROJECT_ROOT"
echo "  IBMCP_BUILD  = $IBMCP_BUILD_DIR"
echo "  BUILD_DIR    = $BUILD_DIR"
echo "  DIST_DIR     = $DIST_DIR"
echo "============================================================"

if [ "$(uname -s)" != "Linux" ]; then
    echo "❌ 此脚本只能在 Linux (openEuler) 上运行"
    exit 1
fi

# 加载驱动库 / Drogon 的 pkg-config 与运行时路径（由 build_drivers.sh / bootstrap.sh 生成）
if [ -f "$IBMCP_BUILD_DIR/env.sh" ]; then
    # shellcheck disable=SC1090
    source "$IBMCP_BUILD_DIR/env.sh"
else
    echo "⚠️  未找到 $IBMCP_BUILD_DIR/env.sh（先运行 bootstrap.sh / build_drivers.sh）"
fi

if ! pkg-config --exists openssl 2>/dev/null; then
    echo "❌ 缺少基础开发库，请先运行: sudo bash deployments/openeuler/bootstrap.sh"
    exit 1
fi

echo ""
echo "[IBMCP] 检查 Drogon 依赖 ..."
# 格式: "显示名|pkg-config 模块名|是否可选(optional 表示不影响构建)"
# 注意: 模块名必须与系统 .pc 文件名一致（去掉 .pc），openEuler 上 libuuid 的 .pc 是 uuid，c-ares 的 .pc 是 libcares
declare -a _deps=(
    "openssl|openssl|required"
    "libuuid|uuid|required"
    "zlib|zlib|required"
    "jsoncpp|jsoncpp|required"
    "c-ares|libcares|required"
    "brotli|libbrotlidec|optional"
)
for entry in "${_deps[@]}"; do
    name="${entry%%|*}"; rest="${entry#*|}"
    mod="${rest%%|*}"; opt="${rest##*|}"
    if pkg-config --exists "$mod" 2>/dev/null; then
        echo "   ✓ $name $(pkg-config --modversion "$mod" 2>/dev/null || echo 'ok')"
    elif [ "$opt" = "optional" ]; then
        echo "   ⚠  $name 未检测到（可选，不影响构建，仅 HTTP 压缩不可用）"
    else
        echo "   ⚠  $name 未检测到，构建可能受影响"
    fi
done

echo ""
echo "[IBMCP] 检查 libpq (PostgreSQL client) ..."
if pkg-config --exists libpq 2>/dev/null; then
    echo "   ✓ libpq $(pkg-config --modversion libpq)"
else
    echo "   ❌ 未找到 libpq，请安装 libpq-devel / postgresql-devel"
    exit 1
fi

echo ""
echo "[IBMCP] 检查 argon2 ..."
if pkg-config --exists libargon2 2>/dev/null; then
    echo "   ✓ libargon2 $(pkg-config --modversion libargon2)"
else
    echo "   ⚠  未找到 libargon2，密码哈希将不可用 (安装 argon2-devel)"
fi

echo ""
echo "[IBMCP] 准备协议驱动库 (libmodbus / libmosquitto / open62541) ..."
need_drivers=0
for p in libmodbus libmosquitto open62541; do
    pkg-config --exists "$p" 2>/dev/null || need_drivers=1
done
if [ "$need_drivers" -eq 1 ]; then
    echo "   检测到缺失驱动库，调用 build_drivers.sh 编译安装（首次约几分钟）..."
    sudo bash "$SCRIPT_DIR/build_drivers.sh" || true
    # shellcheck disable=SC1090
    source "$IBMCP_BUILD_DIR/env.sh" 2>/dev/null || true
fi
for p in libmodbus libmosquitto open62541; do
    if pkg-config --exists "$p" 2>/dev/null; then
        echo "   ✓ $p $(pkg-config --modversion "$p")"
    else
        echo "   ⚠  $p 未就绪 → 该协议将编译为降级（模拟值）模式"
    fi
done

echo ""
echo "[IBMCP] 检查 Drogon ..."
if [ -f "$IBMCP_BUILD_DIR/drogon/cmake/Drogon/DrogonConfig.cmake" ]; then
    echo "   ✓ Drogon (static) @ $IBMCP_BUILD_DIR/drogon"
elif pkg-config --exists drogon 2>/dev/null; then
    echo "   ✓ drogon $(pkg-config --modversion drogon)"
else
    echo "   ❌ 未找到 Drogon，请运行 bootstrap.sh 编译安装到 $IBMCP_BUILD_DIR/drogon"
    exit 1
fi

echo ""
echo "[IBMCP] 配置 CMake: $BUILD_DIR ..."
cmake -S "$PROJECT_ROOT/backend" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
    -DCMAKE_CXX_STANDARD=20 \
    -DCMAKE_INSTALL_PREFIX="$DIST_DIR"

echo ""
echo "[IBMCP] 编译 (make -j$MAKE_JOBS) ..."
cmake --build "$BUILD_DIR" -j"$MAKE_JOBS"

echo ""
echo "[IBMCP] 安装到 $DIST_DIR ..."
cmake --install "$BUILD_DIR"
mkdir -p "$DIST_DIR/logs"

BIN="$DIST_DIR/bin/ibmcp_server"
if [ ! -x "$BIN" ]; then
    echo "   ❌ 未找到 $BIN，检查编译错误"
    exit 1
fi

echo ""
echo "[IBMCP] 运行时库完整性校验 ..."
echo "   二进制: $BIN ($(ls -lh "$BIN" | awk '{print $5}'))"
MISSING="$(ldd "$BIN" 2>/dev/null | grep 'not found' || true)"
if [ -n "$MISSING" ]; then
    echo "   ❌ 存在未解析的动态库："
    echo "$MISSING" | sed 's/^/      /'
    echo "   → 检查 /etc/ld.so.conf.d/ibmcp-build.conf 是否注册并重跑 sudo ldconfig"
    exit 1
else
    echo "   ✓ ldd 无 not found，运行时库齐全"
fi

echo ""
echo "✅ 构建完成"
echo "   产物目录: $DIST_DIR"
echo "     bin/ibmcp_server   后端可执行文件（含采集器）"
echo "     config.json        运行配置"
echo "     migrations/        SQL 迁移脚本"
echo "     logs/              运行日志目录"
echo "   试跑: cd $DIST_DIR && ./bin/ibmcp_server"
echo "   部署: sudo bash deployments/openeuler/deploy.sh"