# CMake 交叉编译 toolchain — IMX6ULL (ARM Cortex-A7, armhf)
#
# 用法：
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=cmake/arm-linux-gnueabihf.cmake
#
# 前提：100ask arm-buildroot-linux-gnueabihf 工具链已位于 PATH

# 交叉编译强制关闭模拟器，避免缓存污染导致误开
set(SIMULATOR OFF CACHE BOOL "Force off for cross-compile" FORCE)

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 工具链前缀
set(CROSS_COMPILE arm-buildroot-linux-gnueabihf-)

set(CMAKE_C_COMPILER   ${CROSS_COMPILE}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_COMPILE}g++)
set(CMAKE_AR           ${CROSS_COMPILE}ar)
set(CMAKE_STRIP        ${CROSS_COMPILE}strip)

# Sysroot：Buildroot SDK 自带的 arm 系统根目录
set(CMAKE_SYSROOT /home/book/100ask_imx6ull-sdk/ToolChain/arm-buildroot-linux-gnueabihf_sdk-buildroot/arm-buildroot-linux-gnueabihf/sysroot)

# 不在宿主机上搜索库和头文件
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
