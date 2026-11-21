#!/usr/bin/env bash
# =============================================================================
# IBMCP openEuler 24.03 一键环境初始化脚本
# 用法：sudo bash deployments/openeuler/bootstrap.sh
# 执行角色：root
#
# 架构（混合模式）：
#   - Docker Compose：PostgreSQL 15 + Redis 7 + Node-RED（纯服务容器）
#   - 裸机：Nginx + C++ 构建工具链（gcc/c++/cmake）
#   - Python 运行时：用欧拉系统自带的 python3.11（不单独装 Python、不装 uv），
#     venv 由 build.sh 创建，_core.so 编译所需的 Python.h 由 build.sh 按需补装
#     python3.11-devel（仅头文件，属于 C++ 构建工具链范畴，不是装解释器）。
#   - 为什么 FastAPI/C++ 不进 Docker：pybind11 扩展必须用宿主 Python 编译，
#     且要访问宿主的 libmodbus.so / open62541.so 和现场 PLC 网段。
#     详见 08 号文档 §1.3。
# =============================================================================
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
COMPOSE_FILE="$PROJECT_ROOT/deployments/openeuler/docker-compose.yml"

echo "============================================================"
echo " IBMCP openEuler 环境初始化（混合模式：Docker + 裸机）"
echo "============================================================"

# --- 0. 基础校验 ---
if [ "$(uname -s)" != "Linux" ]; then
    echo "❌ 此脚本只能在 Linux (openEuler) 上运行"
    exit 1
fi
if [ "$(id -u)" -ne 0 ]; then
    echo "❌ 请用 root 用户执行：sudo bash bootstrap.sh"
    exit 1
fi

# --- 1. SELinux + 防火墙 ---
echo ""
echo "[1/7] 关闭 SELinux 和防火墙（开发环境）..."
if command -v setenforce &>/dev/null; then
    setenforce 0 2>/dev/null || true
    sed -i 's/^SELINUX=enforcing$/SELINUX=disabled/' /etc/selinux/config 2>/dev/null || true
fi
if systemctl is-active --quiet firewalld 2>/dev/null; then
    systemctl stop firewalld
    systemctl disable firewalld
fi
echo "✅ SELinux + 防火墙已关闭"

# --- 2. dnf 安装裸机编译工具链 + Nginx（不装 PG/Redis，走 Docker）---
echo ""
echo "[2/7] 安装 dnf 系统包（编译工具链 + 驱动库 + Nginx）..."

# 必需包：任一失败即中止。
# 注意：不装 python3 / python3-devel / python3-pip —— 欧拉用系统自带的 python3.11，
#   不单独装 Python 解释器；编译 pybind11 扩展所需的 Python.h 由 build.sh 在编译时
#   按需补装 python3.11-devel（仅头文件，属 C++ 构建工具链范畴，见 build.sh）。
# 不装 postgresql-devel —— asyncpg 是预编译 wheel，不需要 libpq 头文件（PG 走 Docker）。
dnf install -y \
    gcc gcc-c++ cmake make pkgconf-pkg-config \
    nginx \
    vim git net-tools wget curl tar unzip \
    || { echo "❌ dnf install 必需包失败"; exit 1; }

# 可选包：协议驱动头文件，缺失时 CMake 自动跳过（QUIET + if(_FOUND)）
#   - mosquitto-devel 在 EPOL 源，可用
#   - libmodbus-devel / open62541-devel 在 openEuler 24.03 SP3 全仓库均无包，
#     需要时从源码编译安装（见 09 号文档 §3.3 可选驱动库）
echo ""
echo "[2/7+] 安装可选协议驱动库（失败不阻断）..."
OPTIONAL_PKGS="mosquitto-devel libmodbus-devel open62541-devel"
for pkg in $OPTIONAL_PKGS; do
    if dnf install -y "$pkg" 2>/dev/null; then
        echo "   ✅ $pkg"
    else
        echo "   ⚠️  $pkg 不可用，跳过（对应协议驱动将在 _core.so 中缺席）"
    fi
done

echo "✅ 裸机必需系统包已安装"
echo "   gcc: $(gcc --version | head -1)"
echo "   cmake: $(cmake --version | head -1)"
echo "   系统自带 python3: $(python3 --version 2>/dev/null || echo '❌ 未找到（openEuler 24.03 应自带 python3.11）')"
echo "   nginx: $(nginx -v 2>&1)"

# --- 3. 安装 Docker CE + docker compose v2 插件（官方源）---
# openEuler 自带仓库只有 docker-engine 18.09 + docker-compose v1.22，
# 不支持 compose v2 语法（本项目的 docker-compose.yml 无 version 键且用
# depends_on.condition，必须 v2）。因此从 Docker 官方 CE 源安装。
echo ""
echo "[3/7] 安装 Docker CE + docker compose v2 插件 ..."

if command -v docker &>/dev/null && docker compose version &>/dev/null; then
    echo "⏭️  Docker CE + compose v2 已存在，跳过安装"
else
    # 移除可能已装的旧版 docker-engine / docker-compose v1，避免命令冲突
    dnf remove -y docker-engine docker-client docker-compose 2>/dev/null || true

    DOCKER_REPO_MIRROR="${DOCKER_REPO_MIRROR:-https://mirrors.tuna.tsinghua.edu.cn/docker-ce/linux/centos}"
    dnf install -y dnf-plugins-core || { echo "❌ dnf-plugins-core 安装失败"; exit 1; }

    # 直接写入 repo 文件（baseurl + gpgkey 全部指向镜像站）。
    # 注意：不能用 dnf config-manager --add-repo，它只添加仓库而 baseurl 仍指向
    # download.docker.com，国内网络会在拉 RPM 时被 SSL reset。
    # openEuler 与 CentOS Stream 9 同属 RHEL 9 系，固定用 9 的路径（不用 $releasever）。
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
        || { echo "❌ Docker CE 安装失败"; exit 1; }

    systemctl enable --now docker
    echo "✅ Docker CE 已安装并启动"
fi

# 将当前执行用户加入 docker 组（非 root 用户可直接用 docker）
CURRENT_USER="${SUDO_USER:-root}"
if [ "$CURRENT_USER" != "root" ]; then
    usermod -aG docker "$CURRENT_USER" 2>/dev/null || true
    echo "✅ 用户 $CURRENT_USER 已加入 docker 组（重新登录后生效）"
fi

docker --version
docker compose version

# --- 4. 启动 Docker Compose（PG15 + Redis + Node-RED）---
echo ""
echo "[4/7] 启动 Docker Compose（PostgreSQL 15 + Redis 7 + Node-RED）..."

if [ ! -f "$COMPOSE_FILE" ]; then
    echo "❌ docker-compose.yml 未找到：$COMPOSE_FILE"
    echo "   请确认项目已同步到 $PROJECT_ROOT"
    exit 1
fi

docker compose -f "$COMPOSE_FILE" up -d

echo "等待容器健康检查 ..."
sleep 5
docker compose -f "$COMPOSE_FILE" ps

# 验证 PG（容器内）
docker exec ibmcp-postgres pg_isready -U ibmcp -d ibmcp \
    && echo "✅ PostgreSQL 容器就绪（ibmcp/ibmcp/ibmcp）" \
    || { echo "❌ PostgreSQL 容器未就绪"; exit 1; }

# 验证 Redis（容器内）
docker exec ibmcp-redis redis-cli ping | grep -q PONG \
    && echo "✅ Redis 容器就绪" \
    || { echo "❌ Redis 容器未就绪"; exit 1; }

echo "✅ Node-RED 容器已启动（http://192.168.80.135:1880）"

# --- 5. Nginx 配置（裸机，反代 FastAPI + 托管前端静态文件）---
echo ""
echo "[5/7] 配置 Nginx（裸机，反代 + 前端静态）..."

NGINX_CONF_SRC="$PROJECT_ROOT/packaging/nginx/ibmcp.conf"
NGINX_CONF_DST="/etc/nginx/conf.d/ibmcp.conf"

if [ -f "$NGINX_CONF_SRC" ]; then
    cp "$NGINX_CONF_SRC" "$NGINX_CONF_DST"
    echo "✅ Nginx 配置已部署到 $NGINX_CONF_DST"
else
    echo "⚠️  $NGINX_CONF_SRC 未找到，跳过（后续手动部署）"
fi

# 创建前端静态目录（nginx.conf 里 root $PROJECT_ROOT/frontend-dist）
mkdir -p "$PROJECT_ROOT/frontend-dist"

# Nginx 以 nginx 用户运行，需要能穿越 $PROJECT_ROOT 及父目录读取静态文件
chmod o+x "$PROJECT_ROOT" "$(dirname "$PROJECT_ROOT")"
chmod -R o+rX "$PROJECT_ROOT/frontend-dist"
echo "✅ 前端静态目录权限已放开（$PROJECT_ROOT/frontend-dist）"

nginx -t 2>&1 && echo "✅ Nginx 配置语法正确" || { echo "❌ Nginx 配置语法错误"; exit 1; }
systemctl enable --now nginx
systemctl is-active --quiet nginx && echo "✅ Nginx active" || { echo "❌ Nginx 启动失败"; exit 1; }

# --- 6. 校验系统自带 python3.11（build.sh 用它建 venv；不装 uv、不单独装 python）---
echo ""
echo "[6/7] 校验系统自带 Python 3.11 ..."
PY_VER="$(python3 -c 'import sys; print(".".join(map(str, sys.version_info[:2])))' 2>/dev/null || echo "")"
if [ "$PY_VER" != "3.11" ]; then
    echo "❌ 未找到系统自带的 Python 3.11（当前: ${PY_VER:-未找到}）"
    echo "   openEuler 24.03 应自带 python3.11。本项目不单独装 Python，请确认系统镜像完整。"
    exit 1
fi
if ! python3 -c "import venv" 2>/dev/null; then
    echo "❌ python3 的 venv 模块不可用"
    exit 1
fi
echo "✅ 系统自带 Python 3.11 就绪：$(python3 --version)"
echo "   不装 uv、不装 python3-pip；venv 自带 pip，build.sh 将用此解释器创建 $PROJECT_ROOT/venv"

# --- 7. 放开 ptrace（CLion gdbserver 远程调试需要）---
echo ""
echo "[7/7] 放开 ptrace_scope（CLion 远程调试需要）..."
echo 0 > /proc/sys/kernel/yama/ptrace_scope
if ! grep -q "kernel.yama.ptrace_scope" /etc/sysctl.conf; then
    echo "kernel.yama.ptrace_scope = 0" >> /etc/sysctl.conf
fi
echo "✅ ptrace_scope 已放开"

# --- 完成 ---
echo ""
echo "============================================================"
echo " ✅ openEuler 环境初始化完成！（混合模式）"
echo ""
echo " 服务状态速览："
echo "   Docker 容器：docker compose -f $COMPOSE_FILE ps"
echo "     ├─ ibmcp-postgres  :5432  (ibmcp/ibmcp/ibmcp)"
echo "     ├─ ibmcp-redis     :6379"
echo "     └─ ibmcp-nodered   :1880"
echo "   裸机："
echo "     ├─ Nginx           :80    (systemctl status nginx)"
echo "     └─ FastAPI         :8000  (尚未启动，见下方步骤)"
echo ""
echo " 接下来的步骤："
echo "   1. 项目代码已在 $PROJECT_ROOT（PyCharm Deployment 自动同步）"
echo "   2. cd $PROJECT_ROOT && bash deployments/openeuler/build.sh  # 编译 C++ 扩展 + venv"
echo "   3. cd $PROJECT_ROOT/backend && cp .env.example .env          # 确认 COLLECTOR_BACKEND=native"
echo "   4. source $PROJECT_ROOT/venv/bin/activate && alembic upgrade head"
echo "   5. 前端部署（在 Windows 侧构建，不在欧拉执行 npm）："
echo "      # —— Windows PowerShell ——"
echo "      cd E:\\999-Dev\\01-IBMCP-黑盒管控系统\\02-IBMCP\\frontend"
echo "      npm install && npm run build"
echo "      scp -r dist\\* meng@192.168.80.135:$PROJECT_ROOT/frontend-dist/"
echo "      # —— 回到欧拉 ——"
echo "      chmod -R o+rX $PROJECT_ROOT/frontend-dist && nginx -s reload"
echo "   6. uvicorn app.main:app --host 0.0.0.0 --port 8000"
echo ""
echo " 验证："
echo "   http://192.168.80.135         → 前端（Nginx 静态）"
echo "   http://192.168.80.135/api/v1/docs → FastAPI OpenAPI（Nginx 反代）"
echo "   http://192.168.80.135:1880    → Node-RED"
echo "============================================================"