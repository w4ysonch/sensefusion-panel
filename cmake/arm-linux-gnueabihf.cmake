# CMake 交叉编译 toolchain — IMX6ULL (ARM Cortex-A7, armhf)
#
# 用法：
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=cmake/arm-linux-gnueabihf.cmake
#
# 前提：
#   1. 安装工具链：sudo apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
#   2. 如需链接板子的系统库，设置 CMAKE_SYSROOT 指向 sysroot 目录

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 工具链前缀
set(CROSS_COMPILE arm-linux-gnueabihf-)

set(CMAKE_C_COMPILER   ${CROSS_COMPILE}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_COMPILE}g++)
set(CMAKE_AR           ${CROSS_COMPILE}ar)
set(CMAKE_STRIP        ${CROSS_COMPILE}strip)

# Cortex-A7 指令集（IMX6ULL）
set(CMAKE_C_FLAGS_INIT
    "-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard -mthumb")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT}")

# Sysroot（可选；若板子库与 Ubuntu 主机不兼容则指定）
# set(CMAKE_SYSROOT ${CMAKE_CURRENT_LIST_DIR}/../sysroot)

# 不在宿主机上搜索库和头文件
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
