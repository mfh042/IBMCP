#!/usr/bin/env bash
# Rebuild Drogon v1.9.5 with BUILD_REDIS=ON (hiredis now installed on Euler).
# Installs to the same prefix /home/meng/ibmbuild/drogon, overwriting the
# current libdrogon.a which contains the Redis "Skipped" stubs.
set -euo pipefail

PREFIX=/home/meng/ibmbuild/drogon
TRANTOR=/home/meng/ibmbuild/trantor
SRC=/tmp/drogon-src

echo "=== [0] backup current libdrogon.a ==="
cp -f "$PREFIX/lib64/libdrogon.a" "$PREFIX/lib64/libdrogon.a.noskip.bak" 2>/dev/null || true
ls -la "$PREFIX/lib64/"

echo "=== [1] clone drogon v1.9.5 (shallow) ==="
rm -rf "$SRC"
ok=0
for url in "https://kkgithub.com/drogonframework/drogon.git" \
           "https://github.com/drogonframework/drogon.git"; do
  echo "  trying $url"
  if git clone --branch v1.9.5 --depth 1 "$url" "$SRC" 2>&1 | tail -3; then ok=1; break; fi
done
[ "$ok" -eq 1 ] || { echo "CLONE FAILED"; exit 1; }

echo "=== [2] init submodule (trantor) ==="
cd "$SRC"
git submodule update --init 2>&1 | tail -3

echo "=== [3] cmake configure with BUILD_REDIS=ON ==="
cmake -S "$SRC" -B "$SRC/build-drogon" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_PREFIX_PATH="$TRANTOR" \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_CTL=ON \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_ORM=ON \
  -DBUILD_REDIS=ON \
  2>&1 | tail -25

echo "  --- redis detection in cmake cache ---"
grep -iE "HIREDIS|REDIS" "$SRC/build-drogon/CMakeCache.txt" 2>/dev/null | head -10 || echo "(no cache entries)"

echo "=== [4] build ==="
cmake --build "$SRC/build-drogon" -j"$(nproc)" 2>&1 | tail -15

echo "=== [5] install ==="
cmake --install "$SRC/build-drogon" 2>&1 | tail -8
ldconfig

echo "=== [6] verify: RedisClient.cc.o (real) present, Skipped gone ==="
ar t "$PREFIX/lib64/libdrogon.a" 2>/dev/null | grep -i "redis" || echo "(no redis objects!)"

echo "=== DONE ==="
