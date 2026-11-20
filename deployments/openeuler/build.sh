#!/usr/bin/env bash
# 欧拉侧构建脚本：创建 venv → 编译 _core 扩展（CMake + pybind11）→ 安装 industrial_collector + backend
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

PYTHON_BIN="${PYTHON_BIN:-python3.11}"
BUILD_DIR="$PROJECT_ROOT/collector-cpp/build"

echo "[IBMCP] 创建 venv（$PYTHON_BIN）..."
"$PYTHON_BIN" -m venv venv
# shellcheck disable=SC1091
source venv/bin/activate
echo "   venv python: $(python --version) -> $(command -v python)"

echo "[IBMCP] 安装 C++ 编译依赖 ..."
dnf install -y cmake gcc-c++ "$PYTHON_BIN-devel" || true

echo "[IBMCP] 安装 pybind11（venv 内，供 CMake 查找）..."
pip install --upgrade pip -q
pip install pybind11 -q

echo "[IBMCP] 编译 _core 扩展（CMake + pybind11）..."
PYBIND11_CMAKE_DIR="$(python -c 'import pybind11; print(pybind11.get_cmake_dir())')"
cmake -S collector-cpp -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -Dpybind11_DIR="$PYBIND11_CMAKE_DIR" \
    -DPython_EXECUTABLE="$VIRTUAL_ENV/bin/python" \
    -DPYTHON_EXECUTABLE="$VIRTUAL_ENV/bin/python"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "[IBMCP] 安装 _core.so 到 python/industrial_collector/ ..."
cmake --install "$BUILD_DIR" --prefix python

echo "[IBMCP] 安装 industrial_collector + backend ..."
pip install .

echo "[IBMCP] ✅ 构建完成"
echo "   验证：python -c 'from industrial_collector import _core; print(_core.Collector.instance().health())'"