#!/usr/bin/env bash
# =============================================================================
# IBMCP openEuler 24.03 一键环境初始化脚本（C++ / Drogon 架构）
# 用法：sudo bash deployments/openeuler/bootstrap.sh
# 执行角色：root
#
# 布局约定（所有 C++ 库/工具统一在用户可写目录，无需污染 /usr/local）：
#   $IBMCP_BUILD_DIR（默认 /home/meng/ibmbuild）
#     ├── drogon/      Drogon 安装前缀（静态库 .a + cmake 配置 + 头文件）
#     ├── trantor/     Trantor 安装前缀（静态库 .a）
#     ├── libmodbus/   libmodbus 前缀（.so + .pc）
#     ├── open62541/   open62541 前缀（.so + .pc）
#     └── env.sh       聚合 PKG_CONFIG_PATH / LD_LIBRARY_PATH / PATH（自动生成）
#   mosquitto          走 dnf 装到系统路径
#   Docker 容器        PG15 / Redis7 / Node-RED
#   Nginx              裸机反代 + 托管前端
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
COMPOSE_FILE="$PROJECT_ROOT/deployments/openeuler/docker-compose.yml"
IBMCP_BUILD_DIR="${IBMCP_BUILD_DIR:-/home/meng/ibmbuild}"
DROGON_VERSION="${DROGON_VERSION:-v1.9.5}"
IBMCP_GITHUB_MIRROR="${IBMCP_GITHUB_MIRROR:-https://kkgithub.com}"

echo "============================================================"
echo " IBMCP openEuler 环境初始化（C++ / Drogon 架构）"
echo " IBMCP_BUILD_DIR = $IBMCP_BUILD_DIR"
echo "============================================================"

if [ "$(uname -s)" != "Linux" ]; then echo "此脚本只能在 Linux (openEuler) 上运行"; exit 1; fi
if [ "$(id -u)" -ne 0 ]; then echo "请用 root 用户执行：sudo bash bootstrap.sh"; exit 1; fi

# ---------------------------------------------------------------------------
# [1/8] SELinux / 防火墙（开发环境）
# ---------------------------------------------------------------------------
echo ""
echo "[1/8] 关闭 SELinux 和防火墙（开发环境）..."
if command -v setenforce &>/dev/null; then
    setenforce 0 2>/dev/null || true
    sed -i 's/^SELINUX=enforcing$/SELINUX=disabled/' /etc/selinux/config 2>/dev/null || true
fi
if systemctl is-active --quiet firewalld 2>/dev/null; then
    systemctl stop firewalld; systemctl disable firewalld
fi
echo "  SELinux + 防火墙已关闭"

# ---------------------------------------------------------------------------
# [2/8] 系统包：C++ 工具链 + DB 客户端 + Nginx + 工具
# ---------------------------------------------------------------------------
echo ""
echo "[2/8] 安装 dnf 系统包（C++ 工具链 + DB 客户端 + Nginx）..."
dnf install -y \
    gcc gcc-c++ cmake make pkgconf-pkg-config gdb \
    libpq-devel libargon2-devel openssl-devel zlib-devel \
    jsoncpp-devel yaml-cpp-devel c-ares-devel util-linux-devel \
    nginx \
    vim git net-tools wget curl tar unzip \
    || { echo " dnf install 必需包失败"; exit 1; }
dnf install -y autoconf automake libtool 2>/dev/null || true
echo "   gcc:   $(gcc --version | head -1)"
echo "   cmake: $(cmake --version | head -1)"
echo "   nginx: $(nginx -v 2>&1)"

# ---------------------------------------------------------------------------
# [3/8] 清理旧 FastAPI systemd 单元（崩溃循环来源）
# ---------------------------------------------------------------------------
echo ""
echo "[3/8] 清理旧 FastAPI systemd 单元（若存在）..."
UNIT=/etc/systemd/system/ibmcp-api.service
if [ -f "$UNIT" ] && grep -qE 'uvicorn|venv|FastAPI' "$UNIT"; then
    systemctl stop ibmcp-api 2>/dev/null || true
    systemctl disable ibmcp-api 2>/dev/null || true
    rm -f "$UNIT"
    systemctl daemon-reload
    echo "  已移除旧 FastAPI 单元 $UNIT"
else
    echo "  无旧 FastAPI 单元，跳过"
fi

# ---------------------------------------------------------------------------
# [4/8] 编译安装 Drogon + Trantor（静态库）→ $IBMCP_BUILD_DIR
# ---------------------------------------------------------------------------
echo ""
echo "[4/8] 编译安装 Drogon $DROGON_VERSION + Trantor（静态）→ $IBMCP_BUILD_DIR ..."
mkdir -p "$IBMCP_BUILD_DIR"

build_trantor_and_drogon() {
    [ -f "$IBMCP_BUILD_DIR/drogon/cmake/Drogon/DrogonConfig.cmake" ] && {
        echo "  Drogon 已就绪 @ $IBMCP_BUILD_DIR/drogon，跳过编译"; return 0; }
    local src=/tmp/drogon-src
    rm -rf "$src"
    local ok=0
    for url in "https://github.com/drogonframework/drogon.git" \
               "${IBMCP_GITHUB_MIRROR}/drogonframework/drogon.git"; do
        if git clone --branch "$DROGON_VERSION" --depth 1 "$url" "$src" 2>/dev/null; then ok=1; break; fi
    done
    [ "$ok" -eq 1 ] || { echo "  Drogon 克隆失败"; return 1; }
    cd "$src"
    git submodule update --init || { echo "  submodule init 失败"; return 1; }

    # Trantor 单独装到 $IBMCP_BUILD_DIR/trantor（与 CMakeLists 的 find 路径对齐）
    echo "  → 编译 Trantor → $IBMCP_BUILD_DIR/trantor"
    cmake -S "$src/trantor" -B "$src/build-trantor" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$IBMCP_BUILD_DIR/trantor" \
        -DBUILD_SHARED_LIBS=OFF \
        || { echo "  Trantor cmake 失败"; return 1; }
    cmake --build "$src/build-trantor" -j"$(nproc)" || { echo "  Trantor 编译失败"; return 1; }
    cmake --install "$src/build-trantor" || { echo "  Trantor 安装失败"; return 1; }

    echo "  → 编译 Drogon → $IBMCP_BUILD_DIR/drogon"
    cmake -S "$src" -B "$src/build-drogon" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$IBMCP_BUILD_DIR/drogon" \
        -DCMAKE_PREFIX_PATH="$IBMCP_BUILD_DIR/trantor" \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_CTL=ON \
        -DBUILD_EXAMPLES=OFF \
        -DBUILD_ORM=ON \
        -DBUILD_REDIS=ON \
        || { echo "  Drogon cmake 失败"; return 1; }
    cmake --build "$src/build-drogon" -j"$(nproc)" || { echo "  Drogon 编译失败"; return 1; }
    cmake --install "$src/build-drogon" || { echo "  Drogon 安装失败"; return 1; }

    # drogon_ctl 放入 PATH（已装到 drogon/bin；同时兼容历史上的 /usr/local/bin）
    mkdir -p "$IBMCP_BUILD_DIR/drogon/bin"
    [ -x "$IBMCP_BUILD_DIR/drogon/bin/drogon_ctl" ] || cp -f "$src/build-drogon/drogon_ctl" "$IBMCP_BUILD_DIR/drogon/bin/" 2>/dev/null || true
    ldconfig
    echo "  Drogon $DROGON_VERSION 已安装到 $IBMCP_BUILD_DIR/drogon"
}
build_trantor_and_drogon || echo "  ⚠️  Drogon 编译失败，后续 build.sh 会再次检查"
echo "   drogon cmake: $([ -f "$IBMCP_BUILD_DIR/drogon/cmake/Drogon/DrogonConfig.cmake" ] && echo OK || echo MISSING)"

# ---------------------------------------------------------------------------
# [5/8] 协议驱动库 + 生成 env.sh + 注册 ldconfig（幂等）
# ---------------------------------------------------------------------------
echo ""
echo "[5/8] 协议驱动库 + env.sh + ldconfig ..."
bash "$SCRIPT_DIR/build_drivers.sh" || echo "  ⚠️  build_drivers.sh 部分失败（退化模式不阻断）"

# ---------------------------------------------------------------------------
# [6/8] Docker CE + compose v2
# ---------------------------------------------------------------------------
echo ""
echo "[6/8] 安装 Docker CE + docker compose v2 插件 ..."
if command -v docker &>/dev/null && docker compose version &>/dev/null; then
    echo "  Docker CE + compose v2 已存在，跳过安装"
else
    dnf remove -y docker-engine docker-client docker-compose 2>/dev/null || true
    DOCKER_REPO_MIRROR="${DOCKER_REPO_MIRROR:-https://mirrors.tuna.tsinghua.edu.cn/docker-ce/linux/centos}"
    dnf install -y dnf-plugins-core || { echo " dnf-plugins-core 安装失败"; exit 1; }
    cat > /etc/yum.repos.d/docker-ce.repo <<EOF
[docker-ce-stable]
name=Docker CE Stable - \$basearch
baseurl=${DOCKER_REPO_MIRROR}/9/\$basearch/stable
enabled=1
gpgcheck=1
gpgkey=${DOCKER_REPO_MIRROR}/gpg
EOF
    dnf install -y docker-ce docker-ce-cli containerd.io \
        docker-buildx-plugin docker-compose-plugin \
        || { echo " Docker CE 安装失败"; exit 1; }
    systemctl enable --now docker
    echo "  Docker CE 已安装并启动"
fi
CURRENT_USER="${SUDO_USER:-root}"
if [ "$CURRENT_USER" != "root" ]; then
    usermod -aG docker "$CURRENT_USER" 2>/dev/null || true
    echo "  用户 $CURRENT_USER 已加入 docker 组（重新登录后免 sudo）"
fi
docker --version; docker compose version

# ---------------------------------------------------------------------------
# [7/8] 启动 Docker Compose（PG15 + Redis7 + Node-RED）
# ---------------------------------------------------------------------------
echo ""
echo "[7/8] 启动 Docker Compose（PostgreSQL 15 + Redis 7 + Node-RED）..."
if [ -f "$COMPOSE_FILE" ]; then
    docker compose -f "$COMPOSE_FILE" up -d
    sleep 5
    docker compose -f "$COMPOSE_FILE" ps
    docker exec ibmcp-postgres pg_isready -U ibmcp -d ibmcp \
        && echo "  PostgreSQL 就绪（ibmcp/ibmcp/ibmcp）" || echo "  ⚠️  PostgreSQL 未就绪"
    docker exec ibmcp-redis redis-cli ping | grep -q PONG && echo "  Redis 就绪" || echo "  ⚠️  Redis 未就绪"
else
    echo "  ⚠️  未找到 $COMPOSE_FILE，跳过容器启动"
fi

# ---------------------------------------------------------------------------
# [8/8] Nginx + ptrace（CLion 远程调试）
# ---------------------------------------------------------------------------
echo ""
echo "[8/8] 配置 Nginx + 放开 ptrace ..."
NGINX_CONF_SRC="$PROJECT_ROOT/packaging/nginx/ibmcp.conf"
NGINX_CONF_DST="/etc/nginx/conf.d/ibmcp.conf"
if [ -f "$NGINX_CONF_SRC" ]; then
    sed -e "s#/home/meng/ibmcp#$PROJECT_ROOT#g" "$NGINX_CONF_SRC" > "$NGINX_CONF_DST"
    echo "  Nginx 配置已部署到 $NGINX_CONF_DST"
fi
mkdir -p "$PROJECT_ROOT/frontend-dist"
chmod o+x "$PROJECT_ROOT" "$(dirname "$PROJECT_ROOT")" 2>/dev/null || true
chmod -R o+rX "$PROJECT_ROOT/frontend-dist" 2>/dev/null || true
nginx -t 2>&1 && echo "  Nginx 配置语法正确" || echo "  ⚠️  Nginx 配置语法错误"
systemctl enable --now nginx 2>/dev/null || true
systemctl is-active --quiet nginx && echo "  Nginx active" || echo "  ⚠️  Nginx 启动失败"

echo 0 > /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || true
grep -q "kernel.yama.ptrace_scope" /etc/sysctl.conf 2>/dev/null || echo "kernel.yama.ptrace_scope = 0" >> /etc/sysctl.conf
echo "  ptrace_scope 已放开（CLion gdbserver 远程调试）"

echo ""
echo "============================================================"
echo " openEuler 环境初始化完成（C++ / Drogon 架构）"
echo ""
echo " 工具链: $(gcc --version | head -1) / cmake $(cmake --version | head -1)"
echo " 构建根: $IBMCP_BUILD_DIR"
echo "   drogon/trantor(静态) + libmodbus/open62541(.so) + env.sh"
echo " 容器 : PG15 :5432 / Redis :6379 / Node-RED :1880"
echo " Nginx: :80（反代 127.0.0.1:8000 + 托管 frontend-dist）"
echo ""
echo " 接下来："
echo "   1. cd $PROJECT_ROOT && bash deployments/openeuler/build.sh"
echo "   2. sudo bash deployments/openeuler/deploy.sh"
echo "   3. 访问 http://$(hostname -I 2>/dev/null | awk '{print $1}')/"
echo "============================================================"
