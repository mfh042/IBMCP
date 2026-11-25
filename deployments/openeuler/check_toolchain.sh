echo "===== C++ 工具链 ====="
for cmd in gcc g++ cmake make pkg-config gdb; do
  if command -v $cmd &>/dev/null; then
    printf "  %-12s OK  %s\n" "$cmd" "$($cmd --version 2>&1 | head -1)"
  else
    printf "  %-12s MISSING\n" "$cmd"
  fi
done

echo ""
IBMCP_BUILD_DIR="${IBMCP_BUILD_DIR:-/home/meng/ibmbuild}"
# shellcheck disable=SC1090
[ -f "$IBMCP_BUILD_DIR/env.sh" ] && source "$IBMCP_BUILD_DIR/env.sh"

echo "===== Drogon + DB 依赖 ====="
if [ -f "$IBMCP_BUILD_DIR/drogon/cmake/Drogon/DrogonConfig.cmake" ]; then
  printf "  %-15s OK  %s\n" "drogon(static)" "$IBMCP_BUILD_DIR/drogon"
else
  printf "  %-15s MISSING\n" "drogon"
fi
for pkg in libpq libargon2 openssl zlib; do
  if pkg-config --exists "$pkg" 2>/dev/null; then
    printf "  %-15s OK  %s\n" "$pkg" "$(pkg-config --modversion "$pkg" 2>/dev/null)"
  else
    printf "  %-15s MISSING\n" "$pkg"
  fi
done

echo ""
echo "===== 协议驱动库 ====="
if [ -d "$IBMCP_BUILD_DIR" ]; then
  echo "  build prefix: $IBMCP_BUILD_DIR"
  for p in libmodbus libmosquitto open62541; do
    if pkg-config --exists "$p" 2>/dev/null; then
      printf "  %-15s OK  %s  libdir=%s\n" "$p" "$(pkg-config --modversion "$p")" "$(pkg-config --variable=libdir "$p")"
    else
      printf "  %-15s MISSING (对应协议将退化模拟值)\n" "$p"
    fi
  done
  echo "  env.sh: $([ -f "$IBMCP_BUILD_DIR/env.sh" ] && echo present || echo MISSING)"
else
  echo "  $IBMCP_BUILD_DIR 不存在（运行 bootstrap.sh / build_drivers.sh）"
fi

echo ""
echo "===== 调试权限 ====="
printf "  ptrace_scope = %s\n" "$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo 'N/A')"