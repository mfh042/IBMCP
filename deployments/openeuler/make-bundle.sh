#!/usr/bin/env bash
# =============================================================================
# IBMCP 离线编译产物打包脚本（openEuler 侧）
#
# 在一台「已成功构建」的机器上运行，把 dist/（二进制 + 配置 + 迁移）+
# $IBMCP_BUILD_DIR（驱动库）+（可选）docker 镜像打成 tarball，
# 拷到其他同架构（x86_64 openEuler 24.03）机器上即可由 deploy.sh --bundle 还原。
#
# 打包内容：
#   1. dist/                 —— bin/ibmcp_server + config.json + migrations/
#   2. ibmbuild/             —— $IBMCP_BUILD_DIR（驱动库 .so + env.sh）
#   3. BUNDLE_INFO.txt       —— 元信息
#   4. images/               —— 可选 docker save 镜像（SAVE_IMAGES=1）
#
# 用法：
#   bash deployments/openeuler/make-bundle.sh
#   BUNDLE_OUT=/data/ibmcp-bundle.tar.gz SAVE_IMAGES=1 bash deployments/openeuler/make-bundle.sh
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"
IBMCP_BUILD_DIR="${IBMCP_BUILD_DIR:-/home/meng/ibmbuild}"
BUNDLE_OUT="${BUNDLE_OUT:-/tmp/ibmcp-bundle-$(date +%Y%m%d-%H%M%S).tar.gz}"
SAVE_IMAGES="${SAVE_IMAGES:-0}"

log()  { echo "[make-bundle] $*"; }
warn() { echo "[make-bundle] WARNING: $*"; }
die()  { echo "[make-bundle] ERROR: $*"; exit 1; }

[ "$(uname -s)" = "Linux" ] || die "此脚本只能在 Linux (openEuler) 上运行"

DIST="$PROJECT_ROOT/dist"
BIN="$DIST/bin/ibmcp_server"
[ -x "$BIN" ] || die "未找到 $BIN，请先运行 bash deployments/openeuler/build.sh"

cd "$PROJECT_ROOT"

log "验证 ibmcp_server ..."
log "   二进制：$BIN ($(ls -lh "$BIN" | awk '{print $5}'))"
log "   动态库依赖："
ldd "$BIN" 2>/dev/null | while read -r line; do log "     $line"; done || true

STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT
BUNDLE_DIR="$STAGE/ibmcp-bundle"; mkdir -p "$BUNDLE_DIR"

log "收集 dist/ ..."
cp -a "$DIST" "$BUNDLE_DIR/dist"

if [ -d "$IBMCP_BUILD_DIR" ]; then
    log "收集 $IBMCP_BUILD_DIR ..."
    cp -a "$IBMCP_BUILD_DIR" "$BUNDLE_DIR/ibmbuild"
else
    warn "未找到 $IBMCP_BUILD_DIR（目标机驱动库将退化）"
fi

if [ "$SAVE_IMAGES" = "1" ] && command -v docker >/dev/null 2>&1; then
    mkdir -p "$BUNDLE_DIR/images"
    for img in postgres:15 redis:7 nodered/node-red:latest; do
        if docker image inspect "$img" >/dev/null 2>&1; then
            safe="$(echo "$img" | tr '/:' '__')"
            log "保存镜像 $img ..."
            docker save "$img" | gzip > "$BUNDLE_DIR/images/${safe}.tar.gz"
        else
            warn "镜像 $img 不存在，跳过"
        fi
    done
fi

{
    echo "IBMCP offline bundle (C++ / Drogon)"
    echo "generated_at:    $(date '+%Y-%m-%d %H:%M:%S %Z')"
    echo "built_host:      $(hostname)"
    echo "project_root:    $PROJECT_ROOT"
    echo "build_dir:       $IBMCP_BUILD_DIR"
    echo "kernel:          $(uname -r)"
    echo "arch:            $(uname -m)"
    echo "drogon(static):  $([ -f "$IBMCP_BUILD_DIR/drogon/cmake/Drogon/DrogonConfig.cmake" ] && echo yes || echo no)"
    echo "docker_images:   $([ "$SAVE_IMAGES" = "1" ] && echo yes || echo no)"
    echo "bin_hash:        $(sha256sum "$BIN" | awk '{print $1}')"
    echo "bin_size:        $(ls -lh "$BIN" | awk '{print $5}')"
} > "$BUNDLE_DIR/BUNDLE_INFO.txt"

log "打包 -> $BUNDLE_OUT ..."
tar czf "$BUNDLE_OUT" -C "$STAGE" ibmcp-bundle
SIZE="$(du -h "$BUNDLE_OUT" | cut -f1)"
log "打包完成：$BUNDLE_OUT ($SIZE)"
echo
echo "  下一步："
echo "    scp $BUNDLE_OUT <目标机>:/tmp/"
echo "    目标机：sudo bash deployments/openeuler/deploy.sh --bundle /tmp/$(basename "$BUNDLE_OUT")"
