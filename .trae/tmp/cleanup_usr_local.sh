#!/bin/bash
set -e

echo "=== [1] 删除前确认将删除的文件 ==="
sudo ls -la /usr/local/lib64/libdrogon.a /usr/local/lib64/libtrantor.a \
  /usr/local/bin/drogon_ctl 2>/dev/null || true

echo
echo "=== [2] 删除 /usr/local 下陈旧 Drogon/Trantor 副本 ==="
sudo rm -f  /usr/local/lib64/libdrogon.a /usr/local/lib64/libtrantor.a
sudo rm -rf /usr/local/lib64/cmake/Drogon /usr/local/lib64/cmake/Trantor
sudo rm -f  /usr/local/lib64/pkgconfig/drogon.pc
sudo rm -rf /usr/local/include/drogon /usr/local/include/trantor
sudo rm -f  /usr/local/bin/drogon_ctl
# 仅删除因此次清理而变空的目录（rmdir 非空会失败，忽略）
sudo rmdir /usr/local/lib64/pkgconfig 2>/dev/null || true
sudo rmdir /usr/local/lib64/cmake     2>/dev/null || true
echo "  完成"

echo
echo "=== [3] 验证 /usr/local 已无 drogon/trantor 残留（为空=干净）==="
sudo find /usr/local \( -iname '*drogon*' -o -iname '*trantor*' \) 2>/dev/null || true
echo "  --- 残留检查结束 ---"

echo
echo "=== [4] /etc/profile.d/ibmcp.sh：drogon_ctl 与 IBMCP_BUILD_DIR 指向 ibmbuild ==="
sudo tee /etc/profile.d/ibmcp.sh >/dev/null <<'EOF'
# IBMCP 工具链路径（由 cleanup/对齐脚本维护）
export IBMCP_BUILD_DIR=/home/meng/ibmbuild
export PATH="/home/meng/ibmbuild/drogon/bin:$PATH"
EOF
sudo chmod 644 /etc/profile.d/ibmcp.sh
echo "  内容："; cat /etc/profile.d/ibmcp.sh

echo
echo "=== [5] 清理误导性构建缓存 cmake-build-debug-oe-remote（含旧 /usr/local 前缀）==="
rm -rf /home/meng/ibmcp/cmake-build-debug-oe-remote && echo "  已删除" || echo "  (不存在)"

echo
echo "=== [6] 确认 ibmbuild 自包含 ==="
ls -d /home/meng/ibmbuild/drogon/cmake/Drogon \
      /home/meng/ibmbuild/trantor/cmake/Trantor \
      /home/meng/ibmbuild/drogon/bin/drogon_ctl 2>/dev/null
echo
echo "DONE"
