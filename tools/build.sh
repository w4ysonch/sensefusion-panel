#!/bin/bash
# 用法：
#   ./tools/build.sh              # PC 模拟器，Debug
#   ./tools/build.sh --release    # PC 模拟器，Release
#   ./tools/build.sh --mqtt       # PC 模拟器 + MQTT
#   ./tools/build.sh --board      # 交叉编译（板子）
#   ./tools/build.sh --board --release --mqtt  # 组合使用
#   ./tools/build.sh --clean      # 清除对应 build 目录后重新编译

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# ── 解析参数 ──────────────────────────────────────────────────────────────
BUILD_TYPE="Debug"
SIMULATOR="ON"
MQTT="OFF"
BOARD="OFF"
CLEAN="OFF"

for arg in "$@"; do
    case "$arg" in
        --release)  BUILD_TYPE="Release" ;;
        --mqtt)     MQTT="ON" ;;
        --board)    BOARD="ON"; SIMULATOR="OFF" ;;
        --clean)    CLEAN="ON" ;;
        --help|-h)
            sed -n '2,9p' "$0" | sed 's/^# //'
            exit 0
            ;;
        *)
            echo "未知参数: $arg，用 --help 查看用法"
            exit 1
            ;;
    esac
done

# ── 确定 build 目录 ───────────────────────────────────────────────────────
if [ "$BOARD" = "ON" ]; then
    BUILD_DIR="${ROOT_DIR}/build-board"
else
    BUILD_DIR="${ROOT_DIR}/build-sim"
fi

# ── 清理 ──────────────────────────────────────────────────────────────────
if [ "$CLEAN" = "ON" ] && [ -d "$BUILD_DIR" ]; then
    echo "[build] 清除 ${BUILD_DIR}"
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

# ── CMake 配置 ────────────────────────────────────────────────────────────
echo "[build] 配置..."
CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
    -DSIMULATOR="${SIMULATOR}"
    -DMQTT="${MQTT}"
)

if [ "$BOARD" = "ON" ]; then
    CMAKE_ARGS+=(-DCMAKE_TOOLCHAIN_FILE="${ROOT_DIR}/cmake/arm-linux-gnueabihf.cmake")
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" "${CMAKE_ARGS[@]}"

# ── 编译 ──────────────────────────────────────────────────────────────────
echo "[build] 编译..."
cmake --build "$BUILD_DIR" -- -j"$(nproc)"

# ── 输出结果 ──────────────────────────────────────────────────────────────
echo ""
echo "[build] 完成 ── ${BUILD_TYPE} / $([ "$BOARD" = "ON" ] && echo "板子" || echo "模拟器") / MQTT=$([ "$MQTT" = "ON" ] && echo "开" || echo "关")"
echo "[build] 产物："
if [ "$BOARD" = "ON" ]; then
    ls -lh "${BUILD_DIR}/sensefusion-daemon" "${BUILD_DIR}/sensefusion-ui" 2>/dev/null || true
else
    ls -lh "${BUILD_DIR}/sensefusion-daemon" "${BUILD_DIR}/sensefusion-ui" 2>/dev/null || true
fi
