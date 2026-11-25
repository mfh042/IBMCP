#!/usr/bin/env bash
# =============================================================================
# IBMCP 一键部署脚本（openEuler 侧，目标机运行）
#
# 一条命令完成：编译后端 → 起 PG 容器 → 执行 SQL 迁移 → 配置 nginx →
#               安装并启动 systemd 服务 → 健康校验。
# 支持从离线 bundle 还原（跳过编译，分钟级部署）。
#
# 前置条件（一次性，由 bootstrap.sh 完成）：
#   - 系统已装 gcc/c++/cmake、$IBMCP_BUILD_DIR 下的 drogon/trantor/驱动库、
#     docker (含 compose)、nginx
#   - 项目源码已同步到目标机
# 若未就绪，先运行：sudo bash deployments/openeuler/bootstrap.sh
#
# 用法：
#   sudo bash deployments/openeuler/deploy.sh
#   sudo bash deployments/openeuler/deploy.sh --bundle /tmp/bundle.tar.gz
#   sudo bash deployments/openeuler/deploy.sh --full            # 含 Redis + Node-RED
#   sudo bash deployments/openeuler/deploy.sh --force-migrate   # 强制重跑迁移
#
# 参数 / 环境变量：
#   --project-root <dir>  | PROJECT_ROOT
#   --user <name>         | SERVICE_USER
#   --port <n>            | SERVICE_PORT   (默认 8000)
#   --bundle <path>       | BUNDLE
#   --frontend-dist <dir> | FRONTEND_DIST
#   --full / --slim       | SLIM           (默认 slim，仅 PG)
#   --force-migrate       | 无视已有表重跑迁移
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
IBMCP_BUILD_DIR="${IBMCP_BUILD_DIR:-/home/meng/ibmbuild}"
SERVICE_USER="${SERVICE_USER:-}"
SERVICE_PORT="${SERVICE_PORT:-8000}"
BUNDLE="${BUNDLE:-}"
FRONTEND_DIST="${FRONTEND_DIST:-}"
SLIM="${SLIM:-1}"
FORCE_MIGRATE="${FORCE_MIGRATE:-0}"

while [ $# -gt 0 ]; do
    case "$1" in
        --project-root)  PROJECT_ROOT="$2"; shift 2 ;;
        --user)          SERVICE_USER="$2"; shift 2 ;;
        --port)          SERVICE_PORT="$2"; shift 2 ;;
        --bundle)        BUNDLE="$2"; shift 2 ;;
        --frontend-dist) FRONTEND_DIST="$2"; shift 2 ;;
        --full)          SLIM=0; shift ;;
        --slim)          SLIM=1; shift ;;
        --force-migrate) FORCE_MIGRATE=1; shift ;;
        -h|--help) sed -n '2,36p' "$0"; exit 0 ;;
        *) echo "未知参数：$1"; exit 1 ;;
    esac
done

log()  { echo "[deploy] $*"; }
warn() { echo "[deploy]  WARNING: $*"; }
die()  { echo "[deploy] ERROR: $*"; exit 1; }

as_root() { if [ "$(id -u)" -eq 0 ]; then "$@"; else sudo "$@"; fi; }
compose() {
    if docker compose version >/dev/null 2>&1; then docker compose "$@"; else docker-compose "$@"; fi
}

[ "$(uname -s)" = "Linux" ] || die "此脚本只能在 Linux (openEuler) 上运行"
[ -d "$PROJECT_ROOT/backend" ] || die "未找到 backend/，PROJECT_ROOT=$PROJECT_ROOT 是否正确？"
[ -d "$PROJECT_ROOT/deployments/openeuler" ] || die "未找到 deployments/openeuler/"

cd "$PROJECT_ROOT"

[ -z "$SERVICE_USER" ] && SERVICE_USER="$(logname 2>/dev/null || whoami)"

log "PROJECT_ROOT   = $PROJECT_ROOT"
log "IBMCP_BUILD    = $IBMCP_BUILD_DIR"
log "SERVICE_USER   = $SERVICE_USER"
log "SERVICE_PORT   = $SERVICE_PORT"

command -v docker >/dev/null || die "未检测到 docker，请先运行 bootstrap.sh"
command -v cmake  >/dev/null || die "未检测到 cmake"
command -v g++    >/dev/null || die "未检测到 g++"
[ -f "$IBMCP_BUILD_DIR/drogon/cmake/Drogon/DrogonConfig.cmake" ] \
    || die "未检测到 Drogon @ $IBMCP_BUILD_DIR/drogon，先运行 bootstrap.sh"

# 加载运行时 / pkg-config 环境（由 build_drivers.sh 生成）
if [ -f "$IBMCP_BUILD_DIR/env.sh" ]; then
    # shellcheck disable=SC1090
    source "$IBMCP_BUILD_DIR/env.sh"
fi

# 确保运行时动态库已注册到 ldconfig（systemd 非登录 shell 不读 env.sh）
if [ ! -f /etc/ld.so.conf.d/ibmcp-build.conf ]; then
    log "注册运行时库到 ldconfig ..."
    bash "$SCRIPT_DIR/build_drivers.sh" >/dev/null 2>&1 || warn "build_drivers.sh 注册失败"
fi
as_root ldconfig >/dev/null 2>&1 || true

# ---------------------------------------------------------------------------
# 1. 编译（或从 bundle 还原）
# ---------------------------------------------------------------------------
BIN="$PROJECT_ROOT/dist/bin/ibmcp_server"
NEED_BUILD=1

if [ -n "$BUNDLE" ]; then
    log "从离线 bundle 还原：$BUNDLE"
    [ -f "$BUNDLE" ] || die "bundle 文件不存在：$BUNDLE"
    stage="$(mktemp -d)"; tar xzf "$BUNDLE" -C "$stage"; bdir="$stage/ibmcp-bundle"
    [ -f "$bdir/BUNDLE_INFO.txt" ] && { log "bundle 元信息："; sed 's/^/   /' "$bdir/BUNDLE_INFO.txt"; }
    if [ -d "$bdir/dist" ]; then
        log "  还原 dist/ ..."; mkdir -p "$PROJECT_ROOT/dist"
        cp -a "$bdir/dist/." "$PROJECT_ROOT/dist/"
        NEED_BUILD=0
    fi
    if [ -d "$bdir/ibmbuild" ]; then
        log "  还原驱动库 -> $IBMCP_BUILD_DIR ..."
        mkdir -p "$IBMCP_BUILD_DIR"
        cp -a "$bdir/ibmbuild/." "$IBMCP_BUILD_DIR/"
        bash "$SCRIPT_DIR/build_drivers.sh" >/dev/null 2>&1 || true
        as_root ldconfig
    fi
    if [ -d "$bdir/images" ]; then
        log "  加载 docker 镜像 ..."
        for img in "$bdir"/images/*.tar.gz; do [ -f "$img" ] || continue; docker load < "$img" || warn "镜像加载失败：$img"; done
    fi
    rm -rf "$stage"
fi

if [ "$NEED_BUILD" -eq 1 ]; then
    log "执行完整编译（build.sh）..."
    bash "$SCRIPT_DIR/build.sh"
fi

[ -x "$BIN" ] || die "编译产物不存在：$BIN"
log "后端二进制：$BIN ($(ls -lh "$BIN" | awk '{print $5}'))"
MISSING="$(ldd "$BIN" 2>/dev/null | grep 'not found' || true)"
[ -z "$MISSING" ] || { echo "$MISSING"; die "存在未解析的动态库（见上），检查 ldconfig"; }
log "  ✓ ldd 无 not found"

# ---------------------------------------------------------------------------
# 2. 启动服务容器
# ---------------------------------------------------------------------------
COMPOSE_ORIG="$PROJECT_ROOT/deployments/openeuler/docker-compose.yml"
if [ "$SLIM" -eq 1 ]; then
    COMPOSE_FILE="$PROJECT_ROOT/deployments/openeuler/docker-compose.prod.yml"
    log "使用精简 compose（仅 PostgreSQL）-> $COMPOSE_FILE"
    cat > "$COMPOSE_FILE" <<'YAML'
services:
  postgres:
    image: postgres:15
    container_name: ibmcp-postgres
    restart: unless-stopped
    environment:
      POSTGRES_USER: ibmcp
      POSTGRES_PASSWORD: ibmcp
      POSTGRES_DB: ibmcp
      TZ: Asia/Shanghai
    ports:
      - "5432:5432"
    volumes:
      - pg_data:/var/lib/postgresql/data
    healthcheck:
      test: [ "CMD-SHELL", "pg_isready -U ibmcp -d ibmcp" ]
      interval: 10s
      timeout: 5s
      retries: 5
volumes:
  pg_data:
    name: ibmcp_pg_data
YAML
else
    COMPOSE_FILE="$COMPOSE_ORIG"
    log "使用完整 compose（PostgreSQL + Redis + Node-RED）"
    [ -f "$COMPOSE_FILE" ] || die "完整 compose 文件不存在"
fi

log "启动服务容器 ..."
compose -f "$COMPOSE_FILE" up -d

log "等待 PostgreSQL 就绪 ..."
for i in $(seq 1 60); do
    docker exec ibmcp-postgres pg_isready -U ibmcp -d ibmcp >/dev/null 2>&1 && { log "  PostgreSQL 已就绪"; break; }
    sleep 2
    [ "$i" -eq 60 ] && die "PostgreSQL 2 分钟内未就绪"
done

# ---------------------------------------------------------------------------
# 3. 数据库迁移（幂等）
# ---------------------------------------------------------------------------
PUBLIC_TABLES="$(docker exec ibmcp-postgres psql -U ibmcp -d ibmcp -tAc \
    "SELECT count(*) FROM information_schema.tables WHERE table_schema='public'" 2>/dev/null || echo 0)"
MIGRATIONS_DIR="$PROJECT_ROOT/dist/migrations"
[ -d "$MIGRATIONS_DIR" ] || MIGRATIONS_DIR="$PROJECT_ROOT/backend/migrations"

if [ "$FORCE_MIGRATE" -eq 1 ] || [ "$PUBLIC_TABLES" -eq 0 ] 2>/dev/null; then
    log "执行 SQL 迁移脚本 ($MIGRATIONS_DIR) ..."
    for f in "$MIGRATIONS_DIR"/*.sql; do
        [ -f "$f" ] || continue
        name="$(basename "$f")"
        docker exec -i ibmcp-postgres psql -U ibmcp -d ibmcp < "$f" >/dev/null 2>&1 \
            && log "  $name" || warn "$name 返回非零"
    done
    log "  SQL 迁移完成"
else
    log "数据库已有 $PUBLIC_TABLES 张表，跳过迁移（如需重跑加 --force-migrate）"
fi

# ---------------------------------------------------------------------------
# 4. 前端静态文件
# ---------------------------------------------------------------------------
if [ -n "$FRONTEND_DIST" ]; then
    [ -d "$FRONTEND_DIST" ] || die "frontend-dist 目录不存在：$FRONTEND_DIST"
    log "部署前端静态文件 -> frontend-dist/ ..."
    rm -rf "$PROJECT_ROOT/frontend-dist"
    cp -a "$FRONTEND_DIST" "$PROJECT_ROOT/frontend-dist"
fi
if [ ! -d "$PROJECT_ROOT/frontend-dist" ] || [ -z "$(ls -A "$PROJECT_ROOT/frontend-dist" 2>/dev/null)" ]; then
    warn "frontend-dist/ 为空，请在 Windows 上 npm run build 后上传（或用 --frontend-dist）"
fi
chmod -R o+rX "$PROJECT_ROOT/frontend-dist" 2>/dev/null || true
chmod o+x "$PROJECT_ROOT" 2>/dev/null || true

# ---------------------------------------------------------------------------
# 5. Nginx 反代
# ---------------------------------------------------------------------------
NGINX_TPL="$PROJECT_ROOT/packaging/nginx/ibmcp.conf"
if command -v nginx >/dev/null 2>&1 && [ -f "$NGINX_TPL" ]; then
    log "配置 nginx 反代 ..."
    TMP_CONF="$(mktemp)"
    sed -e "s#/home/meng/ibmcp#$PROJECT_ROOT#g" "$NGINX_TPL" > "$TMP_CONF"
    as_root cp -f "$TMP_CONF" /etc/nginx/conf.d/ibmcp.conf
    rm -f "$TMP_CONF"
    if as_root nginx -t >/dev/null 2>&1; then
        as_root systemctl enable --now nginx >/dev/null 2>&1 || true
        as_root systemctl reload nginx 2>/dev/null || as_root systemctl restart nginx
        log "  nginx 已配置并启动"
    else
        warn "nginx -t 校验失败"
    fi
else
    warn "跳过 nginx 配置"
fi

# ---------------------------------------------------------------------------
# 6. systemd 服务
# ---------------------------------------------------------------------------
UNIT_TPL="$PROJECT_ROOT/packaging/systemd/ibmcp-api.service"
UNIT_DEST="/etc/systemd/system/ibmcp-api.service"
if [ -f "$UNIT_TPL" ]; then
    log "安装 systemd 服务 ibmcp-api ..."
    TMP_UNIT="$(mktemp)"
    sed -e "s#/home/meng/ibmcp#$PROJECT_ROOT#g" \
        -e "s#^User=meng#User=$SERVICE_USER#" \
        "$UNIT_TPL" > "$TMP_UNIT"
    as_root cp -f "$TMP_UNIT" "$UNIT_DEST"
    rm -f "$TMP_UNIT"
    as_root systemctl daemon-reload
    as_root systemctl enable ibmcp-api >/dev/null 2>&1
    as_root systemctl restart ibmcp-api
    sleep 3
    if as_root systemctl is-active --quiet ibmcp-api; then
        log "  ibmcp-api 服务已启动并设为开机自启"
    else
        warn "ibmcp-api 未正常运行，查看日志：sudo journalctl -u ibmcp-api -n 50 --no-pager"
    fi
else
    warn "未找到 systemd unit 模板：$UNIT_TPL"
fi

# ---------------------------------------------------------------------------
# 7. 健康校验
# ---------------------------------------------------------------------------
HOST_IP="$(hostname -I 2>/dev/null | awk '{print $1}')"; HOST_IP="${HOST_IP:-127.0.0.1}"
log "验证后端健康状态 ..."
OK=0
for i in $(seq 1 20); do
    curl -fsS "http://127.0.0.1:$SERVICE_PORT/api/v1/ping" >/dev/null 2>&1 && { OK=1; break; }
    sleep 2
done

echo
echo "============================================================"
echo " IBMCP 部署完成"
echo "============================================================"
echo " 项目目录  : $PROJECT_ROOT"
echo " 构建根    : $IBMCP_BUILD_DIR"
echo " 后端二进制: $BIN"
echo " 运行用户  : $SERVICE_USER"
echo " 后端端口  : $SERVICE_PORT"
echo " Compose   : $COMPOSE_FILE ($([ "$SLIM" -eq 1 ] && echo '精简:仅PG' || echo '完整:PG+Redis+NodeRED'))"
[ "$OK" -eq 1 ] && echo " 后端健康  : OK" || echo " 后端健康  : 未通过（启动可能较慢，journalctl -u ibmcp-api）"
echo
echo " 访问地址："
echo "   Web 前端  : http://$HOST_IP/"
echo "   后端 API  : http://$HOST_IP/api/v1/ping"
echo "   Node-RED  : http://$HOST_IP:1880"
echo " 常用运维："
echo "   日志   : sudo journalctl -u ibmcp-api -f"
echo "   重启   : sudo systemctl restart ibmcp-api"
echo "   容器   : docker compose -f $COMPOSE_FILE ps"
echo "============================================================"
