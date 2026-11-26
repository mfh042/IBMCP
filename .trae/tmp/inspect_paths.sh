#!/bin/bash
echo '=== 运行中的进程 ==='
ps -ef | grep -i ibmcp_server | grep -v grep
echo
echo '=== dist 二进制时间 ==='
ls -la /home/meng/ibmcp/dist/bin/ibmcp_server 2>/dev/null
echo
echo '=== 各构建目录 CMakeCache 关键值 ==='
for d in /home/meng/ibmcp/backend/build /home/meng/ibmcp/cmake-build-trae /home/meng/ibmcp/cmake-build-debug-oe-remote /home/meng/ibmcp/build; do
  if [ -f "$d/CMakeCache.txt" ]; then
    echo "--- $d ---"
    grep -iE '^(Drogon_DIR|Trantor_DIR|CMAKE_PREFIX_PATH|IBMCP_BUILD_DIR|CMAKE_INSTALL_PREFIX|CMAKE_CXX_COMPILER):' "$d/CMakeCache.txt" 2>/dev/null
  fi
done
echo
echo '=== CMakeLists.txt 修改时间 & 第13-24行 ==='
ls -la /home/meng/ibmcp/backend/CMakeLists.txt
sed -n '13,24p' /home/meng/ibmcp/backend/CMakeLists.txt
echo
echo '=== 两份 DrogonConfig / .a 对比 ==='
ls -la /usr/local/lib64/cmake/Drogon/DrogonConfig.cmake /home/meng/ibmbuild/drogon/cmake/Drogon/DrogonConfig.cmake 2>/dev/null
ls -la /usr/local/lib64/libdrogon.a /home/meng/ibmbuild/drogon/lib64/libdrogon.a 2>/dev/null
ls -la /usr/local/lib64/libtrantor.a /home/meng/ibmbuild/trantor/lib64/libtrantor.a 2>/dev/null
echo
echo '=== /usr/local/lib64 是否有驱动 .so（图上说不变）==='
ls /usr/local/lib64/ 2>/dev/null | grep -iE 'modbus|open62541|mosquitto' || echo '(无——驱动仍在 ibmbuild)'
echo
echo '=== git 状态：CMakeLists / env.sh 是否被改过 ==='
cd /home/meng/ibmcp && git status --short backend/CMakeLists.txt deployments/openeuler/build.sh 2>/dev/null
cd /home/meng/ibmbuild 2>/dev/null && git log --oneline -1 2>/dev/null || true
