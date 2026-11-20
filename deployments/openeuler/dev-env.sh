#!/usr/bin/env bash
# IBMCP 欧拉侧开发环境引导脚本 —— 让终端“用欧拉的环境跑本地代码”
#
# 用法（在欧拉终端里）：
#   source /home/meng/ibmcp/deployments/openeuler/dev-env.sh
# 之后终端就处于：项目根目录 + venv 已激活 + 常用别名可用
#
# 用法（首次自动引导，可在 ~/.bashrc 里加一行）：
#   source /home/meng/ibmcp/deployments/openeuler/dev-env.sh
#
# 注意：必须以 source 方式执行（`. 路径` 或 `source 路径`），
#       直接 `bash dev-env.sh` 只会在子 shell 生效，退出后环境消失。

IBMCP_ROOT="${IBMCP_ROOT:-/home/meng/ibmcp}"
IBMCP_VENV="$IBMCP_ROOT/venv"

# --- 0. 非交互式会话（scp/sftp/ssh 执行单条命令）必须完全静默 ---
# 否则会向 stdout 打日志，破坏 scp 的协议握手（报 "Received message too long"）。
if [ -z "${PS1:-}" ] && [ ! -t 1 ]; then
    case "${-}" in
        *i*) _IBMCP_INTERACTIVE=1 ;;
        *) _IBMCP_INTERACTIVE=0 ;;
    esac
else
    _IBMCP_INTERACTIVE=1
fi
IBMCP_VERBOSE="${IBMCP_VERBOSE:-$_IBMCP_INTERACTIVE}"

_ibmcp_say() {
    [ "$IBMCP_VERBOSE" = "1" ] || return 0
    echo "$@"
}

# --- 1. 只在首次加载时做一次性检查，避免每次开终端都刷屏 ---
if [ -z "${_IBMCP_DEV_ENV_LOADED:-}" ]; then
    _IBMCP_DEV_ENV_LOADED=1

    if [ ! -d "$IBMCP_ROOT" ]; then
        _ibmcp_say "❌ 项目目录不存在：$IBMCP_ROOT"
        return 1 2>/dev/null || exit 1
    fi

    if [ ! -x "$IBMCP_VENV/bin/python" ]; then
        _ibmcp_say "⚠️  venv 不存在：$IBMCP_VENV"
        _ibmcp_say "    先执行：cd $IBMCP_ROOT && bash deployments/openeuler/build.sh"
    elif ! "$IBMCP_VENV/bin/python" -c "import industrial_collector._core" >/dev/null 2>&1; then
        _ibmcp_say "⚠️  _core 扩展不可用（COLLECTOR_BACKEND 请先用 stub，或重跑 build.sh）"
    fi
fi

# --- 2. 激活 venv（已激活则不重复）---
if [ -d "$IBMCP_VENV/bin" ]; then
    if [ "${VIRTUAL_ENV:-}" != "$IBMCP_VENV" ]; then
        # shellcheck disable=SC1091
        . "$IBMCP_VENV/bin/activate" >/dev/null 2>&1 || true
    fi
fi

# --- 3. 默认进入项目根目录 ---
cd "$IBMCP_ROOT" || return 1 2>/dev/null || exit 1

# --- 4. 常用别名（用别名而不是函数，避免污染业务代码命名）---
alias ibmcp-build='cd "$IBMCP_ROOT" && bash deployments/openeuler/build.sh'
alias ibmcp-api='cd "$IBMCP_ROOT/backend" && uvicorn app.main:app --reload --host 0.0.0.0 --port 8000'
alias ibmcp-api-native='cd "$IBMCP_ROOT/backend" && COLLECTOR_BACKEND=native uvicorn app.main:app --reload --host 0.0.0.0 --port 8000'
alias ibmcp-test='cd "$IBMCP_ROOT" && pytest -q backend/tests'
alias ibmcp-migrate='cd "$IBMCP_ROOT/backend" && alembic upgrade head'
alias ibmcp-up='cd "$IBMCP_ROOT" && docker compose -f deployments/openeuler/docker-compose.yml up -d'
alias ibmcp-down='cd "$IBMCP_ROOT" && docker compose -f deployments/openeuler/docker-compose.yml down'
alias ibmcp-logs='cd "$IBMCP_ROOT" && docker compose -f deployments/openeuler/docker-compose.yml logs -f'

# --- 5. 提示符标记，避免和“本地 Windows 终端”混淆 ---
IBMCP_PS1_TAG='[ibmcp] '
case "$PS1" in
    *"$IBMCP_PS1_TAG"*) ;;
    *) PS1="${IBMCP_PS1_TAG}${PS1}" ;;
esac

if [ "$IBMCP_VERBOSE" = "1" ]; then
    _ibmcp_say "✅ IBMCP 开发环境就绪"
    _ibmcp_say "   root : $IBMCP_ROOT"
    _ibmcp_say "   python: $(command -v python 2>/dev/null) ($(python --version 2>&1))"
    _ibmcp_say "   别名 : ibmcp-build / ibmcp-api / ibmcp-api-native / ibmcp-test / ibmcp-migrate / ibmcp-up / ibmcp-down / ibmcp-logs"
fi