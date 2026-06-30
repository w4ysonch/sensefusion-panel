# DESIGN.md — SenseFusion Panel 架构与设计文档

## 事件 Payload 定义

所有事件的 payload 结构体定义在 `app/app_events.h`，本文档与其保持同步。

```c
typedef struct { float temperature; float humidity; }          evt_dht11_t;
typedef struct { float x; float y; float z; float magnitude; } evt_adxl345_t;
typedef struct { uint8_t detected; }                           evt_sr501_t;
typedef struct { float distance_cm; }                          evt_sr04_t;
typedef struct { uint16_t lux; }                               evt_light_t;
typedef struct { int32_t x; int32_t y; uint8_t pressed; }     evt_touch_t;
typedef struct { uint16_t key_code; }                          evt_ir_t;
typedef struct { float heat_index; uint8_t level; }            evt_comfort_t;
typedef struct { uint8_t type; float magnitude; }              evt_anomaly_t;
```

---

## 进程模型与线程模型

系统由两个独立进程构成，通过三条 Linux IPC 通道通信。

### sensor_daemon（7 个线程）

```
sensor_dht11_thread   ┐
sensor_adxl345_thread │  embedmq_post(EVT_*)
sensor_sr501_thread   ├─────────────────────▶ embedmq 消费者线程
sensor_sr04_thread    │                              │
sensor_light_thread   ┘                      daemon_on_*(payload)
                                                     │
                                   ┌─────────────────┼──────────────────┐
                                   │                 │                  │
                             ipc_socket_send   db_log_*()        mqtt_publish_*()
                             ipc_mq_send_alert
```

主线程：`while(g_running) { sleep(1); ipc_shm_read_settings(); }` 轮询阈值变化。

### sensefusion-ui（5 个线程）

```
ipc_recv_thread   ┐  (UDS 接收帧)
ipc_alert_thread  │  (mq 接收告警)    mutex_lock
input_touch       ├──────────────────▶ sensor_cache_t ──────▶ dashboard_tick()
input_ir          ┘  (直接调用)        mutex_unlock           lv_timer_handler()
                                                              (LVGL 主线程)
```

**关键约束：**
- LVGL `LV_USE_OS = LV_OS_NONE`，非线程安全；所有 `lv_*` API **只能**在主线程 `dashboard_tick()` 中调用
- ipc_recv/alert/input 线程只写 `sensor_cache_t`（mutex 保护），不碰 LVGL
- `dashboard_tick()` 先拷贝缓存、清 dirty flag，再在无锁状态下调用 LVGL

---

## embedmq 单处理器约束与链式调用

`embedmq_register` 每个事件 UUID 只接受一个 handler。多个模块响应同一事件时，在 handler 内顺序调用。

**embedmq 仅存在于 sensor_daemon 进程内**，ui_app 不使用 embedmq，改由 IPC 接收线程直接写 `sensor_cache_t`。

```c
// app/daemon_handlers.c — 每个 handler 串联四个操作
void daemon_on_dht11(const void *payload, size_t size, void *ctx) {
    const evt_dht11_t *ev = payload;
    ipc_socket_send(s_sock_fd, &frame);        // 1. 通过 UDS 推送给 ui_app
    algo_comfort_on_dht11(payload, size, NULL); // 2. 触发算法
    db_log_dht11(ev->temperature, ev->humidity);// 3. SQLite 持久化
    mqtt_publish_dht11(ev->temperature, ev->humidity); // 4. MQTT 上报
}
```

算法模块在计算完毕后继续 `embedmq_post(EVT_ALGO_COMFORT)`，由 `daemon_on_comfort` 接收后通过 UDS 推给 ui_app。

---

## SQLite 持久化设计

### Schema

```sql
CREATE TABLE readings (
    id     INTEGER PRIMARY KEY AUTOINCREMENT,
    ts     INTEGER NOT NULL,   -- Unix timestamp（秒）
    sensor TEXT    NOT NULL,   -- 'dht11' | 'adxl345' | 'sr501' | 'sr04' | 'light' | 'comfort' | 'anomaly'
    v1     REAL DEFAULT 0,
    v2     REAL DEFAULT 0,
    v3     REAL DEFAULT 0,
    v4     REAL DEFAULT 0
);
CREATE INDEX idx_sensor_ts ON readings(sensor, ts);
```

各传感器字段映射：

| sensor | v1 | v2 | v3 | v4 |
|---|---|---|---|---|
| dht11 | temperature | humidity | — | — |
| adxl345 | x | y | z | magnitude |
| sr501 | detected | — | — | — |
| sr04 | distance_cm | — | — | — |
| light | lux | — | — | — |
| comfort | heat_index | level | — | — |
| anomaly | type | magnitude | — | — |

### 设计决策

- **WAL 模式 + `synchronous=NORMAL`**：写延迟 < 1 ms，适合 eMMC；读写可并发
- **写入发生在消费者线程**：天然串行，无需额外 mutex
- **SQLite amalgamation 内嵌**（`third_party/sqlite3/sqlite3.c`）：交叉编译时不依赖 sysroot 中的系统库
- **退出时清理旧数据**：`db_cleanup_old(30)` 删除 30 天前记录，`PRAGMA wal_checkpoint(TRUNCATE)` 收缩 WAL 文件
- **手动清理入口**：系统页提供按钮，触发 `db_cleanup_old(30)` 并显示清理后记录条数

---

## EEPROM 持久化配置

### 数据结构

```c
typedef struct {
    float    anomaly_threshold;  // 异常检测阈值，单位 g，默认 0.3
    uint8_t  unit_fahrenheit;    // 0=°C，1=°F
    uint8_t  alert_muted;        // 0=告警开，1=静音
    uint8_t  brightness;         // 背光亮度 0~100
    uint32_t magic;              // 0x5F2025CF，校验数据有效性
} app_settings_t;
```

存储在 EEPROM 地址 `0x0000`，总线 `/dev/i2c-0`，设备地址 `0x50`（需上板确认）。

### 设计决策

- **魔数校验**：首次上电 EEPROM 内容随机，魔数不匹配则写入默认值；避免脏数据导致异常行为
- **页对齐写入**：AT24Cxx 按页（8 字节）写入，跨页时分段，每段后 5ms 延迟等待内部编程周期
- **读取失败重置 fd**：I2C read 失败时 `close(fd); fd = -1`，下次调用重新 `open`，应对总线瞬断

---

## MQTT 发布设计

### 主题与 Payload

主题前缀：`sensefusion/`，QoS 0，不保留（retain=false）。

| 主题 | JSON 字段 |
|---|---|
| `sensefusion/dht11` | `ts`, `temp`, `humi` |
| `sensefusion/adxl345` | `ts`, `x`, `y`, `z`, `mag` |
| `sensefusion/sr501` | `ts`, `detected` |
| `sensefusion/sr04` | `ts`, `dist_cm` |
| `sensefusion/light` | `ts`, `lux` |
| `sensefusion/comfort` | `ts`, `heat_index`, `level` |
| `sensefusion/anomaly` | `ts`, `type`, `magnitude` |

### 设计决策

- **`mosquitto_loop_start()`**：内部启动后台线程驱动 MQTT 事件循环，`mosquitto_publish()` 线程安全，可从消费者线程直接调用
- **`MQTT_ENABLED` 宏**：未定义时所有 `mqtt_publish_*` 编译为空 inline stub，零运行时开销；cmake `-DMQTT=ON` 启用
- **连接失败不阻塞主功能**：`mqtt_init()` 返回 -1 时打印警告继续运行

---

## UI 设计

### 四 Tab 布局（1024×600）

```
+----------------------------------------------+
|  总览  |  趋势  |  设置  |  系统  |  <- Tab bar (44px)
+----------------------------------------------+
|                                              |
|              Tab 内容区（~540px）            |
|                                              |
+----------------------------------------------+
|    [告警横幅（浮动在 screen 最顶层）]         |  y=540, h=52
```

- **总览 Tab**：2 行传感器卡片（温湿度/舒适度/PIR/距离/加速度/光照）
- **趋势 Tab**：5 个 `lv_chart` 折线图（60 点滚动），点击卡片全屏查看
- **设置 Tab**：MQTT/DB 状态 + 调节卡（亮度滑块、温度单位开关、静音开关、异常阈值滑块）
- **系统 Tab**：系统信息（CPU/内存/运行时间）、数据库清理按钮、IR 遥控说明
- **告警横幅**：`lv_obj_create(scr)` 直接挂在 screen 上（非 tabview 子对象），渲染在所有 Tab 之上

### 全屏趋势图

点击趋势页任意图表卡片，弹出全屏详情层（`g_detail_panel`）。详情图通过 `lv_chart_set_series_ext_y_array` 共享小图的 `y_points` 数组，只读展示，无额外内存拷贝。

### IR 遥控 Tab 切换

`input_ir_thread` 捕获 `KEY_LEFT(105)` / `KEY_RIGHT(106)` -> `embedmq_post(EVT_INPUT_IR)` -> `ui_on_ir()` -> `dashboard_handle_ir_key()`。

handler 运行在消费者线程，写入 `cache.ir_key` 和 `cache.ir_dirty`（独立字段，与触摸通道完全分离）；主线程在 `dashboard_tick()` 检测到 `ir_dirty` 后调用 `lv_tabview_set_active()` 循环切换 4 个 Tab。

### 设置控件线程安全

所有设置控件（slider/switch）的回调由 LVGL 在主线程触发，直接修改 `g_settings` 并调用 `settings_save()`，不需要 mutex。`dashboard_tick()` 读取 `g_settings` 也在主线程，无竞争。

阈值回调额外调用 `ipc_shm_write_settings()` 将新值写入共享内存，daemon 主循环每秒读取后调用 `algo_anomaly_set_threshold()` 使其生效。

---

## 跨进程 IPC 设计

### 三通道概览

| 通道 | 机制 | 方向 | 用途 |
|---|---|---|---|
| `/tmp/sensefusion.sock` | Unix Domain Socket | daemon→ui | 传感器数据帧（连续） |
| `/sensefusion_alert` | POSIX 消息队列 | daemon→ui | 异常告警（离散事件） |
| `/sensefusion_settings` | 共享内存 + 信号量 | ui→daemon | 配置同步（anomaly_threshold） |

### UDS 帧格式

```c
typedef struct {
    uint8_t type;    /* ipc_msg_type_t: DHT11/ADXL345/SR501/SR04/LIGHT/COMFORT */
    uint8_t _pad[3];
    union { ipc_dht11_t; ipc_adxl345_t; ... } payload;
} ipc_frame_t;
```

每次 `write/read` 传输固定 `sizeof(ipc_frame_t)` 字节，`ipc_socket_recv` 内部循环读直到收满，防止粘包。

### POSIX 消息队列

- 队列属性：最大 8 条消息，优先级 1（高于默认 0）
- sender 端用 `O_NONBLOCK`：队列满时丢弃告警并打印警告，不阻塞 daemon
- receiver 端阻塞等待，在独立线程（`ipc_alert_thread`）中运行

### 共享内存

- 大小 = `sizeof(app_settings_t)`，命名信号量（初值 1）保护读写
- daemon 以 `O_CREAT` 创建并写入初始值；ui_app 以 `O_RDWR` 打开已有 shm
- 仅 `anomaly_threshold` 字段被 daemon 实际消费，其余字段由 EEPROM 负责持久化

---

## 算法模块

### 舒适度指数（algo/comfort_index.c）

基于 Steadman 热指数公式，输入温度 + 湿度，输出体感温度（heat index）并映射为 5 级：

| level | 描述 | 体感温度 |
|---|---|---|
| 0 | 冷 | HI < 10°C |
| 1 | 凉 | 10 <= HI < 20°C |
| 2 | 舒适 | 20 <= HI < 28°C |
| 3 | 热 | 28 <= HI < 35°C |
| 4 | 酷热 | HI >= 35°C |

**边界守卫**：Steadman 公式仅在温度 >= 20°C 且湿度 >= 40% 时有效，超出范围直接以实测温度分级。

### 异常检测（algo/anomaly.c）

对 ADXL345 合加速度 `|a| = sqrt(x^2+y^2+z^2)` 维护长度 8 的滑动窗口：

- **冷启动保护**：前 8 个样本为预热期，不触发告警（`history_count < HISTORY_LEN` 提前返回）
- **检测顺序**：先用旧历史算均值，与新样本比较后再写入，避免自引用缩小偏差
- **可调阈值**：默认 0.3g，通过 `algo_anomaly_set_threshold()` 运行时修改，设置页滑块实时生效并持久化
- **UI 响应**：告警横幅显示 5 秒后自动消失；触摸屏任意点击提前 dismiss；`alert_muted` 为 1 时静默

---

## CJK 字体生成

`fonts/gen_font.sh` 扫描所有 `*.c` / `*.h`（排除 third_party）提取 CJK 字符（U+4E00~U+9FFF），结合 ASCII 可打印字符 + `°`，用 `lv_font_conv` 生成 14/16/28px 的 `.c` 字体文件。

**注意**：全角标点（，。（）—等，Unicode 在 CJK 区之外）不在扫描范围，UI 字符串中应使用半角替代。新增汉字后重跑脚本重新编译即可。

---

## 开发阶段

### Phase 1 — 基础框架 [x]
- LVGL SDL2 模拟器跑通，显示静态界面
- embedmq 集成，post/register 流程验证
- 完整项目脚手架

### Phase 2 — 模拟器完善 [x]
- 5 路传感器 `#ifdef SIMULATOR` 随机游走数据
- 主循环忙等修复（`usleep` + `lv_timer_handler()` 返回值）
- 告警横幅 5 秒自动消失 + 触摸 dismiss
- 算法 bug 修复（anomaly 冷启动误报、comfort 公式边界守卫）

### Phase 3 — 数据层与网络 [x]
- SQLite WAL 持久化（storage/db.c，sqlite3 amalgamation 内嵌）
- MQTT 异步发布（network/mqtt_client.c，-DMQTT=ON 可选）
- 四 Tab UI + 5 个 lv_chart 趋势折线图 + 全屏查看
- IR 遥控 Tab 切换（独立 ir_dirty 通道）
- CJK 字体（gen_font.sh 自动扫描生成）
- LVGL framebuffer HAL（/dev/fb0 + 触摸 indev）
- Cortex-A7 交叉编译 toolchain（Buildroot SDK）

### Phase 4 — 配置持久化与设置页 [x]
- AT24Cxx EEPROM 驱动（页对齐写入，魔数校验）
- app_settings_t 持久化（亮度/温度单位/静音/异常阈值）
- 设置页交互控件（slider/switch，改动实时写 EEPROM）
- 系统页（/proc 系统信息，数据库清理按钮）
- 异常阈值运行时可调（algo_anomaly_set_threshold）

### Phase 5 — 双进程 IPC 拆分 [x]
- sensor_daemon + sensefusion-ui 两个独立进程
- Unix Domain Socket：传感器数据流（daemon→ui，定长帧）
- POSIX 消息队列：异常告警（daemon→ui，优先级消息）
- 共享内存 + 命名信号量：anomaly_threshold 实时同步（ui→daemon）
- input_touch/ir 解耦 embedmq，直接调用 dashboard 函数
- embedmq 仅保留在 daemon 进程内

### Phase 6 — 板子接入（需硬件）
- DHT11 字符设备驱动
- ADXL345 I2C（地址 0x53/0x1D，数据寄存器 0x32~0x37）
- SR501 / SR04 GPIO sysfs
- 光敏电阻 ADC sysfs
- EEPROM / backlight sysfs 路径确认（`/dev/i2c-?`，`/sys/class/backlight/*/brightness`）
