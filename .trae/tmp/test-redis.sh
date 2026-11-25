#!/usr/bin/env bash
# Redis 接入综合验证（欧拉端）
set +e
PASS=0; FAIL=0
red() { echo -e "\033[31m[FAIL] $*\033[0m"; }
grn() { echo -e "\033[32m[PASS] $*\033[0m"; }
chk() { if [ "$1" = "$2" ]; then grn "$3"; PASS=$((PASS+1)); else red "$3 (期望 '$2' 实际 '$1')"; FAIL=$((FAIL+1)); fi; }

echo "================ 1. 基础设施状态 ================"
SVC=$(sudo systemctl is-active ibmcp-api)
chk "$SVC" "active" "ibmcp-api 服务 active"
RPING=$(sudo docker exec ibmcp-redis redis-cli ping 2>/dev/null)
chk "$RPING" "PONG" "容器 redis-cli PING = PONG"

echo
echo "================ 2. 健康端点（直连 8000）================"
PING_JSON=$(curl -s --max-time 5 http://127.0.0.1:8000/api/v1/ping)
echo "  /api/v1/ping -> $PING_JSON"
echo "$PING_JSON" | grep -q '"code":0' && grn "/api/v1/ping code=0" && PASS=$((PASS+1)) || { red "/api/v1/ping 非 code=0"; FAIL=$((FAIL+1)); }

REDIS_JSON=$(curl -s --max-time 8 http://127.0.0.1:8000/api/v1/redis/ping)
echo "  /api/v1/redis/ping -> $REDIS_JSON"
echo "$REDIS_JSON" | grep -q '"redis":"ok"' && grn "redis 健康 = ok" && PASS=$((PASS+1)) || { red "redis 健康 != ok"; FAIL=$((FAIL+1)); }
echo "$REDIS_JSON" | grep -q '"ping":"PONG"' && grn "PING 返回 PONG" && PASS=$((PASS+1)) || { red "PING 非 PONG"; FAIL=$((FAIL+1)); }
echo "$REDIS_JSON" | grep -q '"get":"1"' && grn "SET/GET 回读 = 1" && PASS=$((PASS+1)) || { red "GET 回读非 1"; FAIL=$((FAIL+1)); }

echo
echo "================ 3. 数据真的落到容器 redis ================"
DBSZ=$(sudo docker exec ibmcp-redis redis-cli DBSIZE 2>/dev/null)
HC=$(sudo docker exec ibmcp-redis redis-cli GET ibmcp:healthcheck 2>/dev/null)
echo "  容器 DBSIZE=$DBSZ  ibmcp:healthcheck=$HC"
[ "$DBSZ" -ge 1 ] 2>/dev/null && grn "容器 DBSIZE >= 1（证明有写入）" && PASS=$((PASS+1)) || { red "容器 DBSIZE < 1"; FAIL=$((FAIL+1)); }
chk "$HC" "1" "容器内 ibmcp:healthcheck = 1"

echo
echo "================ 4. TTL / 过期行为（证明是真 Redis 非内存桩）================"
sudo docker exec ibmcp-redis redis-cli SET ibmcp:ttltest hello EX 2 >/dev/null
V1=$(sudo docker exec ibmcp-redis redis-cli GET ibmcp:ttltest 2>/dev/null)
sleep 3
V2=$(sudo docker exec ibmcp-redis redis-cli GET ibmcp:ttltest 2>/dev/null)
echo "  t=0 GET=$V1 ; t=3s GET=$V2 (期望 hello / 空)"
chk "$V1" "hello" "TTL key 2s 内可读 = hello"
[ -z "$V2" ] && grn "TTL key 过期后消失（真 Redis 行为）" && PASS=$((PASS+1)) || { red "TTL key 未过期（$V2）"; FAIL=$((FAIL+1)); }

echo
echo "================ 5. 幂等 / 稳定：连续 10 次 redis/ping ================"
ok10=0
for i in $(seq 1 10); do
  if curl -s --max-time 5 http://127.0.0.1:8000/api/v1/redis/ping | grep -q '"redis":"ok"'; then ok10=$((ok10+1)); fi
done
chk "$ok10" "10" "连续 10 次 redis/ping 全部 ok ($ok10/10)"

echo
echo "================ 6. 并发：20 并发同时打 redis/ping ================"
rm -f /tmp/redis_conc.log
for i in $(seq 1 20); do
  curl -s --max-time 5 http://127.0.0.1:8000/api/v1/redis/ping >> /tmp/redis_conc.log &
done
wait
CONC_OK=$(grep -c '"redis":"ok"' /tmp/redis_conc.log 2>/dev/null || echo 0)
chk "$CONC_OK" "20" "20 并发全部成功 ($CONC_OK/20)"

echo
echo "================ 7. 回归：登录链路（确保没把现有功能搞坏）================"
TOK=$(curl -s --max-time 8 -X POST http://127.0.0.1:8000/api/v1/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"admin","password":"admin123"}')
echo "  login -> $(echo "$TOK" | head -c 120)..."
echo "$TOK" | grep -q '"code":0' && grn "登录接口 code=0" && PASS=$((PASS+1)) || { red "登录接口非 code=0"; FAIL=$((FAIL+1)); }
echo "$TOK" | grep -q '"token"' && grn "登录返回 token" && PASS=$((PASS+1)) || { red "登录无 token"; FAIL=$((FAIL+1)); }

echo
echo "================ 8. 日志：有无 redis 相关 ERROR/异常 ================"
LOG=/home/meng/ibmcp/dist/logs/ibmcp.log
ERR=$(grep -ciE "redis.*(error|exception|fail)|error.*redis" "$LOG" 2>/dev/null || echo 0)
echo "  日志中 redis 相关 ERROR 行数 = $ERR"
[ "$ERR" -eq 0 ] && grn "无 redis 相关错误日志" && PASS=$((PASS+1)) || { red "存在 redis 相关错误日志（见下）"; grep -iE "redis.*(error|exception|fail)|error.*redis" "$LOG" | tail -5; FAIL=$((FAIL+1)); }
echo "  --- 最近 6 行日志 ---"
tail -6 "$LOG" 2>/dev/null

echo
echo "================ 汇总 ================"
echo "  PASS=$PASS  FAIL=$FAIL"
[ "$FAIL" -eq 0 ] && echo -e "\033[32m✅ 全部通过\033[0m" || echo -e "\033[31m❌ 存在失败\033[0m"
exit $FAIL
