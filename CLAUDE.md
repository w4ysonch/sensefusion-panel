# CLAUDE.md

Context file for AI coding assistants (Claude Code, Cursor, etc.).

## Project Overview

**sensefusion-panel** is a multi-sensor fusion display terminal running on an IMX6ULL Linux board.
Two-process architecture: sensor_daemon (5 sensor threads → embedmq → IPC send) + sensefusion-ui (IPC recv → embedmq → LVGL 1024×600 dark-theme Dashboard).


---

## Directory Structure

```
sensefusion-panel/
├── sensor_daemon.c         daemon process entry (sensors + algo + db + mqtt + IPC send)
├── ui_app.c                UI process entry (LVGL + IPC recv + embedmq + input handling)
├── common/
│   └── app_common.h        Shared: extern g_mq, all EVT_* macros, payload structs
├── daemon/
│   └── daemon_handlers.c/h embedmq callbacks: IPC send + algo + db + mqtt (daemon only)
├── ipc/
│   ├── ipc_protocol.h      Cross-process protocol: frame structs, alert msg, resource name constants
│   ├── ipc_socket.c/h      Unix Domain Socket (sensor data stream, daemon→ui)
│   ├── ipc_mq.c/h          POSIX message queue (anomaly alerts, daemon→ui)
│   └── ipc_shm.c/h         Shared memory + semaphore (config sync, ui→daemon)
├── sensors/                One thread per sensor; real drivers on board (currently TODO stubs)
├── input/                  Touchscreen (MT-B) and IR remote threads (post to embedmq)
├── algo/
│   ├── comfort_index.c/h   Steadman heat index → 5-level comfort rating
│   └── anomaly.c/h         ADXL345 sliding-window (8 samples) anomaly detection, runtime-adjustable threshold
├── ui/
│   ├── ui_dashboard.c/h    LVGL widgets, sensor_cache_t, dashboard_tick()
│   ├── ui_handlers.c/h     embedmq callbacks: dashboard_update_* only (ui only)
│   └── ui_ipc.c/h          IPC receive threads: UDS + mq → embedmq_post (ui only)
├── storage/
│   ├── db.c/h              SQLite WAL persistence (daemon writes; ui reads + cleanup)
│   ├── eeprom.c/h          AT24Cxx I2C EEPROM driver (page-aligned writes, 5ms delay)
│   └── settings.c/h        Persistent config (brightness/unit/mute/threshold, magic validation)
├── network/
│   └── mqtt_client.c/h     libmosquitto async publish, enabled with -DMQTT=ON (daemon only)
├── sim/
│   └── lv_drv_sdl.c/h      PC simulator SDL2 HAL (guarded by #ifdef SIMULATOR)
├── fonts/                  Custom LVGL CJK font .c files (gen_font.sh auto-generates from source)
├── third_party/
│   ├── embedmq/            git submodule (both processes)
│   ├── lvgl/               git submodule (LVGL v9)
│   ├── sqlite3/            SQLite amalgamation (embedded, no system dep)
│   └── lv_conf.h           LVGL config — LV_BUILD_CONF_DIR in CMakeLists.txt points here
└── cmake/                  Cross-compile toolchain (arm-linux-gnueabihf.cmake)
```

---

## Key Conventions

### embedmq: one handler per UUID

`embedmq_register` accepts only one handler per UUID. Both processes have their own independent
`g_mq` instance. Producers and consumers are symmetric across both sides:

**daemon side** — sensor threads post, `daemon_handlers.c` consumes:
```c
// daemon/daemon_handlers.c — each handler chains four operations
void daemon_on_dht11(const void *payload, size_t size, void *ctx) {
    ipc_socket_send(s_sock_fd, &frame);         // 1. push to ui via UDS
    algo_comfort_on_dht11(payload, size, NULL);  // 2. trigger algo
    db_log_dht11(ev->temperature, ev->humidity); // 3. SQLite persistence
    mqtt_publish_dht11(ev->temperature, ev->humidity); // 4. MQTT publish
}
```

**ui side** — ipc_recv/alert threads and input threads post, `ui_handlers.c` consumes:
```c
// ui/ui_handlers.c — only updates sensor_cache_t, no algo/db/mqtt
void ui_on_dht11(const void *payload, size_t size, void *ctx) {
    dashboard_update_dht11(ev->temperature, ev->humidity);
}
```

### LVGL thread safety (LV_USE_OS = LV_OS_NONE)

LVGL is not thread-safe. Solution:

- All producer threads (ipc_recv, ipc_alert, input_touch, input_ir) post to embedmq.
- The embedmq consumer thread (all `ui_on_*` callbacks) **only writes** to `sensor_cache_t` (mutex-protected).
- All LVGL API calls are in `dashboard_tick()`, called exclusively from the **main thread**.
- `dashboard_tick()` ends with `lv_timer_handler()`.

Never call `lv_label_set_text` or any LVGL API from a callback or sensor thread.

### embedmq_handler_fn signature

```c
typedef void (*embedmq_handler_fn)(const void *data, size_t size, void *ctx);
```

Second parameter is `size_t`, not `uint16_t`.

---

## Event Table (common/app_common.h)

| Macro | Payload struct | Producer (daemon) | Producer (ui) |
|---|---|---|---|
| `EVT_SENSOR_DHT11` | `evt_dht11_t` { temperature, humidity } | sensor_dht11 | ipc_recv_thread |
| `EVT_SENSOR_ADXL345` | `evt_adxl345_t` { x, y, z, magnitude } | sensor_adxl345 | ipc_recv_thread |
| `EVT_SENSOR_SR501` | `evt_sr501_t` { detected } | sensor_sr501 | ipc_recv_thread |
| `EVT_SENSOR_SR04` | `evt_sr04_t` { distance_cm } | sensor_sr04 | ipc_recv_thread |
| `EVT_SENSOR_LIGHT` | `evt_light_t` { lux } | sensor_light | ipc_recv_thread |
| `EVT_ALGO_COMFORT` | `evt_comfort_t` { heat_index, level } | algo/comfort_index | ipc_recv_thread |
| `EVT_ALERT_ANOMALY` | `evt_anomaly_t` { type, magnitude } | algo/anomaly | ipc_alert_thread |
| `EVT_INPUT_TOUCH` | `evt_touch_t` { x, y } | — | input_touch |
| `EVT_INPUT_IR` | `evt_ir_t` { key_code } | — | input_ir |

---

## Build

```bash
# PC simulator — builds two binaries
mkdir build && cd build && cmake .. -DSIMULATOR=ON && make -j$(nproc)

# Run (terminal 1 first, then terminal 2)
./sensefusion-daemon
./sensefusion-ui

# Board cross-compile
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-linux-gnueabihf.cmake && make -j$(nproc)
```

After cloning: `git submodule update --init --recursive`

---

## TODO

- [ ] Sensor drivers: DHT11 char device, ADXL345 I2C, SR501/SR04 GPIO sysfs, light ADC sysfs
- [ ] Confirm EEPROM device node (/dev/i2c-?) and backlight sysfs path on board
