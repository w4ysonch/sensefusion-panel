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

# ── 第三方库 ───────────────────────────────────────────────────────────────
set(EMBEDMQ_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(EMBEDMQ_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/embedmq)

set(LV_BUILD_CONF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
    CACHE PATH "Directory containing lv_conf.h" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS   OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/lvgl)

# ── 公共 IPC + 配置源文件（两个 target 共用）─────────────────────────────
set(COMMON_SOURCES
    ipc/ipc_socket.c
    ipc/ipc_mq.c
    ipc/ipc_shm.c
    storage/settings.c
    storage/eeprom.c
)

# ── sensefusion-daemon ────────────────────────────────────────────────────
set(DAEMON_SOURCES
    sensor_daemon.c
    daemon/daemon_handlers.c
    sensors/sensor_dht11.c
    sensors/sensor_adxl345.c
    sensors/sensor_sr501.c
    sensors/sensor_sr04.c
    sensors/sensor_light.c
    algo/comfort_index.c
    algo/anomaly.c
    storage/db.c
    network/mqtt_client.c
    third_party/sqlite3/sqlite3.c
)

add_executable(sensefusion-daemon ${DAEMON_SOURCES} ${COMMON_SOURCES})

target_include_directories(sensefusion-daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3
)

target_link_libraries(sensefusion-daemon PRIVATE
    embedmq
    m
    pthread
    rt      # POSIX mq + shm
    dl      # SQLite 动态扩展
)

# ── sensefusion-ui ────────────────────────────────────────────────────────
set(UI_SOURCES
    ui_app.c
    ui/ui_dashboard.c
    ui/ui_handlers.c
    ui/ui_ipc.c
    input/input_touch.c
    input/input_ir.c
    storage/db.c
    third_party/sqlite3/sqlite3.c
    fonts/lv_font_sf_sc_14.c
    fonts/lv_font_sf_sc_16.c
    fonts/lv_font_sf_sc_28.c
)

if(SIMULATOR)
    find_package(SDL2 REQUIRED)
    list(APPEND UI_SOURCES
        sim/lv_drv_sdl.c
        sim/mouse_cursor_icon.c
    )
endif()

add_executable(sensefusion-ui ${UI_SOURCES} ${COMMON_SOURCES})

target_include_directories(sensefusion-ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3
)

target_link_libraries(sensefusion-ui PRIVATE
    embedmq
    lvgl
    m
    pthread
    rt      # POSIX mq + shm
    dl      # SQLite 动态扩展
    $<$<BOOL:${SIMULATOR}>:${SDL2_LIBRARIES}>
)

# ── MQTT（可选，daemon 侧）────────────────────────────────────────────────
if(MQTT)
    find_library(MOSQUITTO_LIB mosquitto)
    find_path(MOSQUITTO_INC mosquitto.h)
    if(NOT MOSQUITTO_LIB OR NOT MOSQUITTO_INC)
        message(FATAL_ERROR
            "找不到 libmosquitto。\n"
            "  Ubuntu/Debian: sudo apt install libmosquitto-dev")
    endif()
    target_compile_definitions(sensefusion-daemon PRIVATE MQTT_ENABLED=1)
    target_include_directories(sensefusion-daemon PRIVATE ${MOSQUITTO_INC})
    target_link_libraries(sensefusion-daemon PRIVATE ${MOSQUITTO_LIB})
    message(STATUS "MQTT 已启用: ${MOSQUITTO_LIB}")
endif()

# ── 编译警告（两个 target 共用）──────────────────────────────────────────
foreach(tgt sensefusion-daemon sensefusion-ui)
    target_compile_options(${tgt} PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        $<$<CONFIG:Debug>:-g3>
        $<$<CONFIG:Release>:-O2 -DNDEBUG>
    )
endforeach()

# ── AddressSanitizer ───────────────────────────────────────────────────────
if(ASAN)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING "ASAN 建议在 Debug 构建下使用")
    endif()
    foreach(tgt sensefusion-daemon sensefusion-ui)
        target_compile_options(${tgt} PRIVATE -fsanitize=address,undefined)
        target_link_options(${tgt}    PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()

# ── 交叉编译（IMX6ULL）──────────────────────────────────────────────────────
# 用法：cmake -DCMAKE_TOOLCHAIN_FILE=cmake/arm-linux-gnueabihf.cmake ..
