#!/usr/bin/env bash
# =============================================================================
# IBMCP openEuler 24.03 一键环境初始化脚本
# 用法：sudo bash deployments/openeuler/bootstrap.sh
# 执行角色：root
#
# 架构（混合模式）：
#   - Docker Compose：PostgreSQL 15 + Redis 7 + Node-RED（纯服务容器）
#   - 裸机：Python 3.11 + venv + FastAPI + C++ 扩展 + Nginx（前端静态 + 反代）
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

dnf install -y \
    gcc gcc-c++ cmake make pkgconf-pkg-config \
    python3.11 python3.11-pip python3.11-devel postgresql-devel \
    libmodbus-devel mosquitto-devel open62541-devel \
    nginx \
    vim git net-tools wget curl tar unzip \
    || { echo "❌ dnf install 失败"; exit 1; }

echo "✅ 裸机系统包已安装"
echo "   gcc: $(gcc --version | head -1)"
echo "   cmake: $(cmake --version | head -1)"
echo "   python3.11: $(python3.11 --version)"
echo "   nginx: $(nginx -v 2>&1)"

# --- 3. 安装 Docker + docker compose 插件 ---
echo ""
echo "[3/7] 安装 Docker + docker compose 插件 ..."

if ! command -v docker &>/dev/null; then
    dnf install -y docker docker-compose-plugin \
        || { echo "❌ Docker 安装失败"; exit 1; }
    systemctl enable --now docker
    echo "✅ Docker 已安装并启动"
else
    echo "⏭️  Docker 已存在，跳过安装"
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

echo "✅ Node-RED 容器已启动（http://192.168.80.134:1880）"

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

# 创建前端静态目录（nginx.conf 里 root /opt/ibmcp/frontend-dist）
mkdir -p /opt/ibmcp/frontend-dist

nginx -t 2>&1 && echo "✅ Nginx 配置语法正确" || { echo "❌ Nginx 配置语法错误"; exit 1; }
systemctl enable --now nginx
systemctl is-active --quiet nginx && echo "✅ Nginx active" || { echo "❌ Nginx 启动失败"; exit 1; }

# --- 6. 安装 uv ---
echo ""
echo "[6/7] 安装 uv ..."
if ! command -v uv &>/dev/null; then
    curl -LsSf https://astral.sh/uv/install.sh | sh
    source "$HOME/.bashrc"
fi
uv --version && echo "✅ uv 已就绪"

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
echo "   1. 项目代码已在 /opt/ibmcp（PyCharm Deployment 自动同步）"
echo "   2. cd /opt/ibmcp && bash deployments/openeuler/build.sh  # 编译 C++ 扩展 + venv"
echo "   3. cd /opt/ibmcp/backend && cp .env.example .env          # 确认 COLLECTOR_BACKEND=native"
echo "   4. source /opt/ibmcp/venv/bin/activate && alembic upgrade head"
echo "   5. 前端构建并部署到 Nginx："
echo "      cd /opt/ibmcp/frontend && npm install && npm run build"
echo "      cp -r dist/* /opt/ibmcp/frontend-dist/"
echo "   6. uvicorn app.main:app --host 0.0.0.0 --port 8000"
echo ""
echo " 验证："
echo "   http://192.168.80.134         → 前端（Nginx 静态）"
echo "   http://192.168.80.134/api/v1/docs → FastAPI OpenAPI（Nginx 反代）"
echo "   http://192.168.80.134:1880    → Node-RED"
echo "============================================================"