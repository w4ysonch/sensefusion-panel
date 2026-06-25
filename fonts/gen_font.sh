#!/usr/bin/env bash
# fonts/gen_font.sh
#
# 扫描项目源码中的汉字，用 lv_font_conv 生成 LVGL 字体 .c 文件。
# 新增中文文案后重新跑此脚本即可，无需手动维护字符集。
#
# 用法：
#   cd /path/to/sensefusion-panel
#   bash fonts/gen_font.sh
#
# 前置条件：
#   npm install -g lv_font_conv

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# ── 字体文件路径（按实际安装位置修改）──────────────────────────
FONT_PATH="/home/wayson/workspace/aipl-ui/aipl/thirdparty/lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf"

# ── 生成的字号（与代码里用到的 lv_font_montserrat_* 对应）──────
SIZES=(14 16 28)

# ── 检查依赖 ───────────────────────────────────────────────────
if ! command -v lv_font_conv &>/dev/null; then
    echo "[错误] 未找到 lv_font_conv，请先安装："
    echo "       npm install -g lv_font_conv"
    exit 1
fi

if [ ! -f "$FONT_PATH" ]; then
    echo "[错误] 字体文件不存在: $FONT_PATH"
    echo "       请修改脚本顶部的 FONT_PATH 变量"
    exit 1
fi

# ── 提取汉字 ───────────────────────────────────────────────────
echo "正在扫描源码汉字..."

CJK_CHARS=$(grep -rh --include="*.c" --include="*.h" \
    --exclude-dir=third_party \
    -P "[\x{4e00}-\x{9fff}]" \
    "$PROJECT_ROOT" 2>/dev/null \
    | grep -oP "[\x{4e00}-\x{9fff}]" \
    | sort -u \
    | tr -d '\n')

if [ -z "$CJK_CHARS" ]; then
    echo "[警告] 未找到汉字，检查源码路径是否正确"
    exit 1
fi

CHAR_COUNT=$(echo "$CJK_CHARS" | grep -oP "[\x{4e00}-\x{9fff}]" | wc -l)
echo "找到 ${CHAR_COUNT} 个不重复汉字"

# ASCII 可打印字符 + 常用符号（°用于°C显示）
ASCII=' !"#$%&'"'"'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz{|}~°'

ALL_SYMBOLS="${ASCII}${CJK_CHARS}"

# ── 生成各字号 ─────────────────────────────────────────────────
for SIZE in "${SIZES[@]}"; do
    OUT="${SCRIPT_DIR}/lv_font_sf_sc_${SIZE}.c"
    echo "生成 ${SIZE}px → $(basename "$OUT") ..."
    lv_font_conv \
        --font "$FONT_PATH" \
        --size "$SIZE" \
        --bpp 4 \
        --format lvgl \
        --symbols "$ALL_SYMBOLS" \
        -o "$OUT"
done

echo ""
echo "完成。生成文件："
for SIZE in "${SIZES[@]}"; do
    echo "  fonts/lv_font_sf_sc_${SIZE}.c  →  LV_FONT_DECLARE(lv_font_sf_sc_${SIZE})"
done
echo ""
echo "使用方式（ui_dashboard.c）："
echo "  将 &lv_font_montserrat_14 替换为 &lv_font_sf_sc_14"
echo "  将 &lv_font_montserrat_16 替换为 &lv_font_sf_sc_16"
echo "  将 &lv_font_montserrat_28 替换为 &lv_font_sf_sc_28"
