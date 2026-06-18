cmake_minimum_required(VERSION 3.16)
project(sensefusion-panel C CXX)  # CXX 是因为 LVGL 内置 ThorVG 用 C++，链接时需要 g++

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── 编译选项 ───────────────────────────────────────────────────────────────
# PC 模拟器模式（SDL2 窗口）：cmake -DSIMULATOR=ON ..
# 板子模式（framebuffer）：  cmake -DSIMULATOR=OFF ..（默认）
option(SIMULATOR "Build for PC simulator with SDL2" OFF)

# ── embedmq ────────────────────────────────────────────────────────────────
add_subdirectory(third_party/embedmq)

# ── LVGL ──────────────────────────────────────────────────────────────────
# lv_conf.h 放在 third_party/（lvgl 的上一级），LVGL CMake 会自动找到它
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
)

# 模拟器模式额外增加 SDL2 HAL
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
)

target_compile_definitions(sensefusion-panel PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    $<$<BOOL:${SIMULATOR}>:SIMULATOR=1>
)

target_link_libraries(sensefusion-panel PRIVATE
    embedmq
    lvgl
    m
    pthread
$<$<BOOL:${SIMULATOR}>:${SDL2_LIBRARIES}>
)

# ── 交叉编译（IMX6ULL）──────────────────────────────────────────────────────
# 用法：cmake -DCMAKE_TOOLCHAIN_FILE=cmake/arm-linux-gnueabihf.cmake ..
