#!/usr/bin/env bash
# IBMCP 欧拉侧开发环境引导脚本
#
# 用法（在欧拉终端里）：
#   source /home/meng/ibmcp/deployments/openeuler/dev-env.sh
# 之后终端就处于：项目根目录 + 驱动库环境已加载 + 常用别名可用
#
# 也可在 ~/.bashrc 里加一行 source 自动引导。

IBMCP_ROOT="${IBMCP_ROOT:-/home/meng/ibmcp}"
IBMCP_BUILD_DIR="${IBMCP_BUILD_DIR:-/home/meng/ibmbuild}"
IBMCP_DIST="$IBMCP_ROOT/dist"

if [ -z "${_IBMCP_DEV_ENV_LOADED:-}" ]; then
    _IBMCP_DEV_ENV_LOADED=1

    if [ ! -d "$IBMCP_ROOT" ]; then
        echo "IBMCP 项目目录不存在：$IBMCP_ROOT"
        return 1 2>/dev/null || exit 1
    fi

    if [ ! -d "$IBMCP_ROOT/backend" ]; then
        echo "未找到 backend/，项目结构异常"
        return 1 2>/dev/null || exit 1
    fi

    if [ -f "$IBMCP_BUILD_DIR/env.sh" ]; then
        # shellcheck disable=SC1091
        source "$IBMCP_BUILD_DIR/env.sh"
    fi

    if [ ! -f "$IBMCP_BUILD_DIR/drogon/cmake/Drogon/DrogonConfig.cmake" ]; then
        echo "未检测到 Drogon @ $IBMCP_BUILD_DIR/drogon，请先运行 bootstrap.sh"
    fi
fi

cd "$IBMCP_ROOT" || return 1 2>/dev/null || exit 1

alias ibmcp-build='cd "$IBMCP_ROOT" && bash deployments/openeuler/build.sh'
alias ibmcp-run='cd "$IBMCP_DIST" && ./bin/ibmcp_server'
alias ibmcp-deploy='cd "$IBMCP_ROOT" && sudo bash deployments/openeuler/deploy.sh'
alias ibmcp-migrate='cd "$IBMCP_ROOT/backend/migrations" && for f in *.sql; do docker exec -i ibmcp-postgres psql -U ibmcp -d ibmcp < "$f" && echo "  $f"; done'
alias ibmcp-up='cd "$IBMCP_ROOT" && docker compose -f deployments/openeuler/docker-compose.yml up -d'
alias ibmcp-down='cd "$IBMCP_ROOT" && docker compose -f deployments/openeuler/docker-compose.yml down'
alias ibmcp-logs='cd "$IBMCP_ROOT" && docker compose -f deployments/openeuler/docker-compose.yml logs -f'
alias ibmcp-psql='docker exec -it ibmcp-postgres psql -U ibmcp -d ibmcp'

IBMCP_PS1_TAG='[ibmcp] '
case "$PS1" in
    *"$IBMCP_PS1_TAG"*) ;;
    *) PS1="${IBMCP_PS1_TAG}${PS1}" ;;
esac