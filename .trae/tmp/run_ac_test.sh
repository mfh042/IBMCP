#!/bin/bash
echo "=== [1] 确保 admin 启用 ==="
sudo docker exec ibmcp-postgres psql -U ibmcp -d ibmcp -c "UPDATE sys_users SET enabled=true WHERE username='admin';" 2>&1 | tail -2

echo
echo "=== [2] 启动 ibmcp-api 服务 ==="
sudo systemctl start ibmcp-api
sleep 2
systemctl is-active ibmcp-api

echo
echo "=== [3] 等待 /api/v1/ping 就绪（最多 30s）==="
for i in $(seq 1 30); do
  code=$(curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:8000/api/v1/ping 2>/dev/null)
  if [ "$code" = "200" ]; then echo "  就绪 (http=$code) 用时 ${i}s"; break; fi
  sleep 1
done

echo
echo "=== [4] 运行 ac_test.py ==="
cd /tmp
python3 ac_test.py > /tmp/ac_out.txt 2>&1
RC=$?
cat /tmp/ac_out.txt

echo
echo "=== [5] 汇总 ==="
PASS=$(grep -c '^PASS' /tmp/ac_out.txt)
FAIL=$(grep -c '^FAIL' /tmp/ac_out.txt)
echo "PASS=$PASS  FAIL=$FAIL  (ac_test exit=$RC)"
[ "$FAIL" -eq 0 ] && echo "RESULT: ALL PASS" || echo "RESULT: HAS FAILURES"

echo
echo "--- 服务日志尾部（确认无 /usr/local 相关报错）---"
sudo tail -15 /home/meng/ibmcp/dist/logs/ibmcp.log 2>/dev/null || true
