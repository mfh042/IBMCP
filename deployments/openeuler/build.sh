#!/usr/bin/env bash
# 欧拉侧构建脚本：创建 venv → 编译 _core 扩展（CMake + pybind11）→ 安装 industrial_collector + backend
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$PROJECT_ROOT"

PYTHON_BIN="${PYTHON_BIN:-python3}"
BUILD_DIR="$PROJECT_ROOT/collector-cpp/build"
PIP_INDEX="${PIP_INDEX:-https://pypi.tuna.tsinghua.edu.cn/simple}"
PIP_OPTS="--index-url $PIP_INDEX --timeout 120 --retries 5"

# 校验：必须用系统自带的 Python 3.11（欧拉不单独装 Python、不用 uv）
PY_VER="$("$PYTHON_BIN" -c 'import sys; print(".".join(map(str, sys.version_info[:2])))' 2>/dev/null || echo "")"
if [ "$PY_VER" != "3.11" ]; then
    echo "❌ 需要 Python 3.11，当前 $PYTHON_BIN = ${PY_VER:-未找到}"
    echo "   请用系统自带的 python3（openEuler 24.03 自带 python3.11），或设置 PYTHON_BIN=python3.11"
    exit 1
fi

echo "[IBMCP] 创建 venv（$PYTHON_BIN）..."
"$PYTHON_BIN" -m venv venv
# shellcheck disable=SC1091
source venv/bin/activate
echo "   venv python: $(python --version) -> $(command -v python)"

echo "[IBMCP] 检查 C++ 编译依赖 ..."
# 工具链兜底（bootstrap.sh 已装，此处幂等）
sudo dnf install -y cmake gcc-c++ make >/dev/null 2>&1 || true
# 编译 pybind11 扩展必须有 Python.h；欧拉系统自带 python3.11 默认不带 -devel，
# 这里按需补装 python3.11-devel（仅是头文件，不是装 Python 解释器）。
PY_INCLUDE="$("$PYTHON_BIN" -c 'import sysconfig; print(sysconfig.get_path("include"))' 2>/dev/null || echo "")"
if [ -z "$PY_INCLUDE" ] || [ ! -f "$PY_INCLUDE/Python.h" ]; then
    echo "   Python.h 缺失，按需安装 python3-devel（仅头文件，用于编译 _core.so）..."
    sudo dnf install -y python3.11-devel 2>/dev/null \
        || sudo dnf install -y python3-devel \
        || {
        echo "❌ 安装 python3-devel 失败，无法编译 pybind11 扩展"
        exit 1
    }
else
    echo "   Python.h 已就绪：$PY_INCLUDE/Python.h"
fi

echo "[IBMCP] 安装 pybind11（venv 内，供 CMake 查找）..."
# shellcheck disable=SC2086
pip install $PIP_OPTS --upgrade pip -q
# shellcheck disable=SC2086
pip install $PIP_OPTS pybind11 -q

echo "[IBMCP] 编译 _core 扩展（CMake + pybind11）..."
PYBIND11_CMAKE_DIR="$(python -c 'import pybind11; print(pybind11.get_cmake_dir())')"
cmake -S collector-cpp -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -Dpybind11_DIR="$PYBIND11_CMAKE_DIR" \
    -DPython_EXECUTABLE="$VIRTUAL_ENV/bin/python" \
    -DPYTHON_EXECUTABLE="$VIRTUAL_ENV/bin/python"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "[IBMCP] 安装 _core.so 到 collector-python/industrial_collector/ ..."
cmake --install "$BUILD_DIR" --prefix collector-python

echo "[IBMCP] 安装 industrial_collector + backend ..."
# shellcheck disable=SC2086
pip install $PIP_OPTS .

echo "[IBMCP] ✅ 构建完成"
echo "   验证：python -c 'from industrial_collector import _core; print(_core.Collector.instance().health())'"