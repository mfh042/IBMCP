#!/usr/bin/env bash
# sync.sh — 把本目录的 Node-RED 物料同步进运行中容器 ibmcp-nodered 并重启
# 用法：cd deployments/nodered && bash sync.sh   （需要 sudo docker 权限）
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NR="${NR_CONTAINER:-ibmcp-nodered}"
FLOW_SRC="$DIR/flows/simulator-modbus.json"
PKG_SRC="$DIR/package.json"

if [ ! -f "$FLOW_SRC" ]; then echo "ERR: $FLOW_SRC not found"; exit 1; fi
if [ ! -f "$PKG_SRC" ];  then echo "ERR: $PKG_SRC not found"; exit 1; fi

echo "==> 容器 $NR 当前状态："
sudo docker ps --filter "name=^/${NR}$" --format '{{.Names}} {{.Status}}' || true

echo "==> 同步 flows.json 与 package.json 到容器 /data"
sudo docker cp "$FLOW_SRC" "$NR:/data/flows.json"
sudo docker cp "$PKG_SRC"  "$NR:/data/package.json"

echo "==> 容器内安装 palette 节点（/data/node_modules）"
if ! sudo docker exec "$NR" sh -lc 'cd /data && npm install --omit=dev --no-audit --no-fund 2>&1 | tail -5'; then
    echo "WARN: npm install 失败（节点可能已存在，或容器无外网）；继续重启"
fi

echo "==> 重启容器使流与节点生效"
sudo docker restart "$NR"

echo "==> 等待启动后做一次健康检查（编辑器需鉴权 → 应 401）"
sleep 6
sudo docker exec "$NR" sh -lc 'wget -q -O- --server-response http://127.0.0.1:1880/settings 2>&1 | head -1' || true
echo "done. 编辑器: http://<host>:1880  Modbus从站: <host>:1502"
