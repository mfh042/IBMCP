#!/bin/bash
echo "=== [1] drogon_ctl 解析路径（应为 /home/meng/ibmbuild/drogon/bin/drogon_ctl）==="
bash -lc 'which drogon_ctl' || echo '(未找到)'

echo
echo "=== [2] /usr/local drogon/trantor 残留（应为空）==="
sudo find /usr/local \( -iname '*drogon*' -o -iname '*trantor*' \) 2>/dev/null || true
echo "--- 残留检查结束（空=干净）---"

echo
echo "=== [3] 全仓搜索 /usr/local 引用（源码/脚本/文档，应为 0 条配置性引用）==="
cd /home/meng/ibmcp
grep -rn --include='*.txt' --include='*.cmake' --include='*.sh' --include='*.json' --include='*.md' '/usr/local' . 2>/dev/null | grep -v '\.trae/tmp' | head -20 || echo '(无)'

echo
echo "=== [4] build.sh 重建 ==="
cd /home/meng/ibmcp
bash deployments/openeuler/build.sh > /tmp/build.log 2>&1
RC=$?
echo "build.sh exit=$RC"
tail -20 /tmp/build.log
if [ "$RC" -ne 0 ]; then
  echo "!!! BUILD FAILED !!!"
  grep -iE 'error:|fatal error|CMake Error' /tmp/build.log | tail -25
  exit 1
fi

echo
echo "=== [5] 重建后 CMakeCache 关键值（应全指向 ibmbuild）==="
grep -iE '^(Drogon_DIR|Trantor_DIR|CMAKE_INSTALL_PREFIX):' /home/meng/ibmcp/backend/build/CMakeCache.txt

echo
echo "=== [6] 二进制时间与依赖 ==="
ls -la /home/meng/ibmcp/dist/bin/ibmcp_server
ldd /home/meng/ibmcp/dist/bin/ibmcp_server 2>/dev/null | grep -iE 'not found|modbus|open62541|drogon|trantor' || echo '(无 not found)'

echo
echo "ALL DONE"
