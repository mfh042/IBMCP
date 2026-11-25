#!/usr/bin/env bash
set +e
echo "================ A. 并发诊断：20 并发的 HTTP 状态码 ================"
rm -f /tmp/c_*.out /tmp/c_*.code
for i in $(seq 1 20); do
  curl -s --max-time 8 -o /tmp/c_$i.out -w "%{http_code}" http://127.0.0.1:8000/api/v1/redis/ping > /tmp/c_$i.code &
done
wait
OK=$(grep -l '"redis":"ok"' /tmp/c_*.out 2>/dev/null | wc -l)
C200=$(grep -h . /tmp/c_*.code 2>/dev/null | grep -c 200)
echo "  HTTP 200 数 = $C200 / 20 ; body 含 redis:ok = $OK / 20"
echo "  非 200 的样本："
for i in $(seq 1 20); do
  code=$(cat /tmp/c_$i.code 2>/dev/null)
  if [ "$code" != "200" ]; then echo "    #$i code=$code body=$(cat /tmp/c_$i.out 2>/dev/null | head -c 200)"; fi
done
# 也看下日志此刻是否有异常
echo "  --- 此刻日志最后 5 行 ---"
tail -5 /home/meng/ibmcp/dist/logs/ibmcp.log

echo
echo "================ B. 登录诊断：先看 captcha 是否必需 ================"
CAP=$(curl -s --max-time 5 http://127.0.0.1:8000/api/v1/auth/captcha)
echo "  captcha -> $(echo "$CAP" | head -c 200)"
echo
echo "  不带 captcha 登录 admin/admin123:"
curl -s --max-time 8 -X POST http://127.0.0.1:8000/api/v1/auth/login \
  -H 'Content-Type: application/json' -d '{"username":"admin","password":"admin123"}'
echo
echo
echo "================ C. 确认当前跑的二进制是新编的（含 redis）================"
ls -la /home/meng/ibmcp/dist/bin/ibmcp_server
echo "  二进制是否引用 hiredis (应能看到 libhiredis.so):"
ldd /home/meng/ibmcp/dist/bin/ibmcp_server 2>/dev/null | grep -i hiredis
echo "  进程启动时间:"
ps -o lstart= -p $(pgrep -f ibmcp_server | head -1) 2>/dev/null
