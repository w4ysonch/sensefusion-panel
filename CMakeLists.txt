cmake_minimum_required(VERSION 3.16)
project(sensefusion-panel C CXX)
# CXX 因为 LVGL 内置 ThorVG 用 C++，链接时需要 g++

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 默认 Debug 构建；Release：cmake -DCMAKE_BUILD_TYPE=Release ..
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type (Debug/Release)" FORCE)
endif()

# ── 构建选项 ───────────────────────────────────────────────────────────────
# PC 模拟器（SDL2 窗口）：cmake -DSIMULATOR=ON ..
option(SIMULATOR "Build for PC simulator with SDL2" OFF)
if(SIMULATOR)
    add_compile_definitions(SIMULATOR=1)
endif()

# AddressSanitizer + UBSan：cmake -DASAN=ON ..
option(ASAN "Enable AddressSanitizer and UBSanitizer" OFF)

# MQTT 支持（需 libmosquitto-dev）：cmake -DMQTT=ON ..
option(MQTT "Enable MQTT publishing via libmosquitto" OFF)

# ── embedmq ────────────────────────────────────────────────────────────────
add_subdirectory(third_party/embedmq)

# ── LVGL ──────────────────────────────────────────────────────────────────
# LV_BUILD_CONF_DIR 必须在 add_subdirectory 之前设置，
# LVGL v9 cmake 通过此变量定位 lv_conf.h。
set(LV_BUILD_CONF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
    CACHE PATH "Directory containing lv_conf.h" FORCE)
add_subdirectory(third_party/lvgl)

# ── Sources ────────────────────────────────────────────────────────────────
set(SOURCES
    main.c
    app/app_init.c
    sensors/sensor_dht11.c
    sensors/sensor_adxl345.c
    sensors/sensor_sr501.c
    sensors/sensor_sr04.c
    sensors/sensor_light.c
    input/input_touch.c
    input/input_ir.c
    algo/comfort_index.c
    algo/anomaly.c
    ui/ui_handlers.c
    ui/ui_dashboard.c
    storage/eeprom.c
    storage/db.c
    network/mqtt_client.c
    third_party/sqlite3/sqlite3.c
    fonts/lv_font_sf_sc_14.c
    fonts/lv_font_sf_sc_16.c
    fonts/lv_font_sf_sc_28.c
)

# 模拟器额外增加 SDL2 HAL
if(SIMULATOR)
    find_package(SDL2 REQUIRED)
    list(APPEND SOURCES
        sim/lv_drv_sdl.c
        sim/mouse_cursor_icon.c
    )
endif()

add_executable(sensefusion-panel ${SOURCES})

target_include_directories(sensefusion-panel PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3
)


# ── SQLite3（自带 amalgamation，无需系统 libsqlite3）──────────────────────
# third_party/sqlite3/sqlite3.c + sqlite3.h 直接参与编译；
# 好处：交叉编译时不依赖 sysroot 中是否安装 sqlite3。

# ── MQTT（可选）────────────────────────────────────────────────────────────
if(MQTT)
    find_library(MOSQUITTO_LIB mosquitto)
    find_path(MOSQUITTO_INC mosquitto.h)
    if(NOT MOSQUITTO_LIB OR NOT MOSQUITTO_INC)
        message(FATAL_ERROR
            "找不到 libmosquitto。\n"
            "  Ubuntu/Debian: sudo apt install libmosquitto-dev")
    endif()
    target_compile_definitions(sensefusion-panel PRIVATE MQTT_ENABLED=1)
    target_include_directories(sensefusion-panel PRIVATE ${MOSQUITTO_INC})
    target_link_libraries(sensefusion-panel PRIVATE ${MOSQUITTO_LIB})
    message(STATUS "MQTT 已启用: ${MOSQUITTO_LIB}")
endif()

# ── 编译警告 ────────────────────────────────────────────────────────────────
target_compile_options(sensefusion-panel PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter       # LVGL 回调 ctx 经常不用
    $<$<CONFIG:Debug>:-g3>
    $<$<CONFIG:Release>:-O2 -DNDEBUG>
)

# ── AddressSanitizer ───────────────────────────────────────────────────────
if(ASAN)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING "ASAN 建议在 Debug 构建下使用")
    endif()
    target_compile_options(sensefusion-panel PRIVATE -fsanitize=address,undefined)
    target_link_options(sensefusion-panel    PRIVATE -fsanitize=address,undefined)
endif()

# ── 链接库 ────────────────────────────────────────────────────────────────
target_link_libraries(sensefusion-panel PRIVATE
    embedmq
    lvgl
    m
    pthread
    $<$<BOOL:${SIMULATOR}>:${SDL2_LIBRARIES}>
)

# SQLite amalgamation 本身链接时依赖 dl（动态加载扩展用）
target_link_libraries(sensefusion-panel PRIVATE dl)

# ── 交叉编译（IMX6ULL）──────────────────────────────────────────────────────
# 用法：cmake -DCMAKE_TOOLCHAIN_FILE=cmake/arm-linux-gnueabihf.cmake ..
