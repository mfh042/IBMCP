#!/usr/bin/env bash
# =============================================================================
# clear-root.sh — 清空欧拉项目根，但保留运行产物
#
# 用途：把 /home/meng/ibmcp 下的源码/缓存/杂项全部清掉，只保留运行产物
#       （dist / frontend-dist），以便随后用 eSync 重新同步最新源码。
#       相当于「重置源码到干净态，不动已构建的二进制和前端」。
#
# 保留项（不会被删）：
#   dist/            后端编译产物（eSync 不传，删了服务起不来）
#   frontend-dist/   前端构建产物（eSync 不传，单独上传）
#   cmake-build-*/   CLion 远程构建缓存（eSync 不同步，删了需 Reload CMake 才重建，故保留）
#   .git/            版本控制（强制保留，防御性；欧拉端通常不存在）
#   --keep 指定的额外目录
#
# 用法：
#   bash clear-root.sh                 # 交互式，会确认
#   bash clear-root.sh -y              # 跳过确认
#   bash clear-root.sh --dry-run       # 只预览将删/将保留，不实际删除
#   bash clear-root.sh --root /path    # 指定项目根（默认 /home/meng/ibmcp）
#   bash clear-root.sh --keep data     # 额外保留 data 目录（可多次）
#
# 清空后建议立即（在 Windows 侧）运行 eSync 重新同步源码。
# =============================================================================
set -uo pipefail

# ---- 默认配置 ----
ROOT="${IBMCP_ROOT:-/home/meng/ibmcp}"
DRY_RUN=0
ASSUME_YES=0
# 强制保留（不可移除）：版本控制
FORCE_KEEP=(".git")
# 默认保留：运行产物（删了服务/前端起不来）
KEEP_DIRS=("dist" "frontend-dist")
# 默认保留（glob 模式）：CLion 远程构建目录缓存
#   eSync 同步源码时排除 cmake-build-*，clear-root 若删掉它，
#   CLion 远程构建会报 "build 目录不存在"，必须 Reload CMake 才重建，故保留。
KEEP_PATTERNS=("cmake-build-*")

# ---- 颜色 ----
if [ -t 1 ]; then
  C_RED=$'\033[31m'; C_GREEN=$'\033[32m'; C_YELLOW=$'\033[33m'
  C_CYAN=$'\033[36m'; C_DIM=$'\033[2m'; C_OFF=$'\033[0m'
else
  C_RED=''; C_GREEN=''; C_YELLOW=''; C_CYAN=''; C_DIM=''; C_OFF=''
fi

# ---- 参数解析 ----
while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run) DRY_RUN=1; shift ;;
    -y|--yes)  ASSUME_YES=1; shift ;;
    --root)    ROOT="$2"; shift 2 ;;
    --keep)    KEEP_DIRS+=("$2"); shift 2 ;;
    -h|--help)
      sed -n '2,30p' "$0"; exit 0 ;;
    *) echo "未知参数: $1" >&2; exit 2 ;;
  esac
done

# ---- 前置检查 ----
if [ ! -d "$ROOT" ]; then
  echo "${C_RED}错误：项目根不存在: $ROOT${C_OFF}" >&2
  exit 1
fi
# 安全护栏：不允许对系统关键目录操作
case "$ROOT" in
  /|/home|/home/meng|/root|/usr|/etc|/var|/tmp)
    echo "${C_RED}错误：拒绝清空系统目录: $ROOT${C_OFF}" >&2; exit 1 ;;
esac

echo "${C_CYAN}==== 清空项目根（保留运行产物）====${C_OFF}"
echo "目标: $ROOT"
[ "$DRY_RUN" -eq 1 ] && echo "${C_YELLOW}[dry-run] 仅预览，不实际删除${C_OFF}"
echo ""

# ---- 汇总保留列表（强制 + 默认 + 追加）----
ALL_KEEP=("${FORCE_KEEP[@]}" "${KEEP_DIRS[@]}")
is_kept() {
  local name="$1" k pat
  for k in "${ALL_KEEP[@]}"; do [ "$name" = "$k" ] && return 0; done
  for pat in "${KEEP_PATTERNS[@]}"; do [[ "$name" == $pat ]] && return 0; done
  return 1
}
is_force_kept() {
  local name="$1" k
  for k in "${FORCE_KEEP[@]}"; do [ "$name" = "$k" ] && return 0; done
  return 1
}

# ---- 枚举顶层条目，分类 ----
to_delete=()
to_keep=()
while IFS= read -r -d '' entry; do
  name="$(basename "$entry")"
  if is_kept "$name"; then
    to_keep+=("$name")
  else
    to_delete+=("$name")
  fi
done < <(find "$ROOT" -mindepth 1 -maxdepth 1 -print0 2>/dev/null)

echo "${C_GREEN}将保留（${#to_keep[@]} 项）：${C_OFF}"
if [ ${#to_keep[@]} -eq 0 ]; then
  echo "  ${C_DIM}(无)${C_OFF}"
else
  for name in "${to_keep[@]}"; do
    tag=""
    is_force_kept "$name" && tag=" ${C_DIM}[强制保留]${C_OFF}"
    echo "  ${C_GREEN}+ $name${C_OFF}$tag"
  done
fi
echo ""

echo "${C_RED}将删除（${#to_delete[@]} 项）：${C_OFF}"
if [ ${#to_delete[@]} -eq 0 ]; then
  echo "  ${C_DIM}(无，已是干净状态)${C_OFF}"
else
  for name in "${to_delete[@]}"; do
    echo "  ${C_RED}- $name${C_OFF}"
  done
fi
echo ""

# ---- dry-run 到此为止 ----
if [ "$DRY_RUN" -eq 1 ]; then
  echo "${C_YELLOW}[dry-run] 未执行任何删除。去掉 --dry-run 后正式运行。${C_OFF}"
  exit 0
fi

# 无可删项，直接结束
if [ ${#to_delete[@]} -eq 0 ]; then
  echo "${C_GREEN}没有需要删除的内容，项目根已是干净状态。${C_OFF}"
  exit 0
fi

# ---- 确认门禁 ----
if [ "$ASSUME_YES" -ne 1 ]; then
  echo -n "${C_YELLOW}确认删除以上 ${#to_delete[@]} 项？输入 yes 继续: ${C_OFF}"
  read -r ans
  if [ "$ans" != "yes" ]; then
    echo "${C_DIM}已取消。${C_OFF}"; exit 0
  fi
fi

# ---- 执行删除（逐个，捕获失败）----
echo ""
echo "${C_CYAN}开始删除...${C_OFF}"
failed=0
deleted=0
for name in "${to_delete[@]}"; do
  target="$ROOT/$name"
  if rm -rf -- "$target" 2>/tmp/clear-root-err; then
    if [ ! -e "$target" ]; then
      echo "  ${C_GREEN}已删${C_OFF} $name"
      deleted=$((deleted+1))
    else
      echo "  ${C_RED}失败${C_OFF} $name (删除后仍存在)"
      failed=$((failed+1))
    fi
  else
    echo "  ${C_RED}失败${C_OFF} $name"
    sed 's/^/      /' /tmp/clear-root-err 2>/dev/null
    failed=$((failed+1))
  fi
done
rm -f /tmp/clear-root-err

echo ""
echo "${C_CYAN}==== 结果验证（门禁）====${C_OFF}"
echo "已删除: $deleted  失败: $failed"

# ---- 门禁：列目录，证明只剩保留项 ----
echo ""
echo "项目根当前内容 (ls -la $ROOT):"
ls -la "$ROOT"
echo ""

# 校验：是否有非保留项残留
leftover=()
while IFS= read -r -d '' entry; do
  name="$(basename "$entry")"
  is_kept "$name" || leftover+=("$name")
done < <(find "$ROOT" -mindepth 1 -maxdepth 1 -print0 2>/dev/null)

if [ ${#leftover[@]} -gt 0 ]; then
  echo "${C_RED}⚠ 仍有非保留项残留：${leftover[*]}${C_OFF}" >&2
  echo "${C_DIM}可能是文件被占用（如运行中的进程）或权限不足。请手动检查。${C_OFF}" >&2
  exit 1
fi

if [ "$failed" -gt 0 ]; then
  echo "${C_RED}有 $failed 项删除失败，请查看上方错误。${C_OFF}" >&2
  exit 1
fi

echo "${C_GREEN}✓ 清空完成，项目根仅保留：${to_keep[*]}${C_OFF}"
echo ""
echo "${C_DIM}下一步（在 Windows 侧）：${C_OFF}"
echo "  ${C_CYAN}. .\deployments\openeuler\euler-remote.ps1${C_OFF}"
echo "  ${C_CYAN}eSync${C_OFF}   ${C_DIM}# 重新同步最新源码到 $ROOT${C_OFF}"