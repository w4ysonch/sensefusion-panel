# DESIGN.md — SenseFusion Panel 架构与设计文档

## 事件 Payload 定义

所有事件的 payload 结构体定义在 `app/app_events.h`，本文档与其保持同步。

```c
typedef struct { float temperature; float humidity; }        evt_dht11_t;
typedef struct { float x; float y; float z; float magnitude; } evt_adxl345_t;
typedef struct { uint8_t detected; }                         evt_sr501_t;
typedef struct { float distance_cm; }                        evt_sr04_t;
typedef struct { uint16_t lux; }                             evt_light_t;
typedef struct { int32_t x; int32_t y; uint8_t pressed; }   evt_touch_t;
typedef struct { uint16_t key_code; }                        evt_ir_t;
typedef struct { float heat_index; uint8_t level; }          evt_comfort_t;
typedef struct { uint8_t type; float magnitude; }            evt_anomaly_t;
```

---

## 线程模型

系统共有 **9 个线程**：5 个传感器线程、2 个输入线程、1 个 embedmq 消费者线程、1 个主线程（LVGL）。

```
传感器 / 输入线程          embedmq 消费者线程              主线程
       │                         │                          │
 embedmq_post(EVT_*)       ui_on_*(payload)           dashboard_tick()
                                 │                          │
                          mutex_lock(&g_mutex)        mutex_lock(&g_mutex)
                          cache.field = value         local = cache
                          cache.dirty = true          cache.dirty = false
                          mutex_unlock()              mutex_unlock()
                                 │                          │
                          db_log_*()                  lv_label_set_text(...)
                          mqtt_publish_*()            lv_chart_set_next_value(...)
                                                      lv_timer_handler() → wait_ms
                                                           │
                                                      usleep(wait_ms - 1)
```

**关键约束：**
- LVGL `LV_USE_OS = LV_OS_NONE`，非线程安全；所有 `lv_*` API **只能**在主线程 `dashboard_tick()` 中调用
- 消费者线程只写 `sensor_cache_t`（mutex 保护）+ SQLite + MQTT，不碰 LVGL
- `dashboard_tick()` 先拷贝缓存、清 dirty flag，再在无锁状态下调用 LVGL

---

## embedmq 单处理器约束与链式调用

`embedmq_register` 每个事件 UUID 只接受一个 handler。多个模块响应同一事件时，在 handler 内顺序调用：

```c
// ui_handlers.c — 每个 handler 串联四个操作
void ui_on_dht11(const void *payload, size_t size, void *ctx) {
    const evt_dht11_t *ev = payload;
    dashboard_update_dht11(ev->temperature, ev->humidity); // 1. 写 UI 缓存
    algo_comfort_on_dht11(payload, size, NULL);             // 2. 触发算法
    db_log_dht11(ev->temperature, ev->humidity);            // 3. SQLite 持久化
    mqtt_publish_dht11(ev->temperature, ev->humidity);      // 4. MQTT 上报
}
```

算法模块在计算完毕后继续 `embedmq_post(EVT_ALGO_COMFORT)`，由 `ui_on_comfort` 接收并更新舒适度卡片。

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

- **WAL 模式 + `synchronous=NORMAL`**：写延迟 < 1 ms，适合 eMMC；读写可并发（若将来主线程需要读历史数据）
- **写入发生在消费者线程**：天然串行，无需额外 mutex
- **SQLite amalgamation 内嵌**（`third_party/sqlite3/sqlite3.c`）：交叉编译时不依赖 sysroot 中的系统库
- **退出时清理旧数据**：`db_cleanup_old(30)` 删除 30 天前记录，`PRAGMA wal_checkpoint(TRUNCATE)` 收缩 WAL 文件

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

### 三 Tab 布局（800×480）

```
┌──────────────────────────────────────────────┐
│  总览  │  趋势  │  设置  │  ← Tab bar (44px) │
├──────────────────────────────────────────────┤
│                                              │
│              Tab 内容区（436px）             │
│                                              │
└──────────────────────────────────────────────┘
│    ⚠ 告警横幅（浮动在 screen 最顶层）         │  y=420, h=52
```

- **总览 Tab**：2 行 × 传感器卡片，沿用原有布局
- **趋势 Tab**：5 个 `lv_chart`（折线，60 点滚动），float 值整数化后写入（×10 或 ×100）
- **设置 Tab**：MQTT 连接状态、SQLite 路径与状态、IR 遥控说明
- **告警横幅**：`lv_obj_create(scr)` 直接挂在 screen 上（非 tabview 子对象），渲染在所有 Tab 之上

### IR 遥控 Tab 切换

`input_ir_thread` 捕获 `KEY_LEFT(105)` / `KEY_RIGHT(106)` → `embedmq_post(EVT_INPUT_IR)` → `ui_on_ir()` → `dashboard_handle_ir_key()`。

由于 `lv_tabview_set_active()` 必须在主线程调用，handler 借用 `touch_dirty` 通道传递切换信号：将 `cache.touch_x` 设为 `-1`（左）或 `-2`（右），主线程在 `dashboard_tick()` 检测到负值后执行 Tab 切换。

---

## 算法模块

### 舒适度指数（algo/comfort_index.c）

基于 Steadman 热指数公式，输入温度 + 湿度，输出体感温度（heat index）并映射为 5 级：

| level | 描述 | 体感温度 |
|---|---|---|
| 0 | 冷 | HI < 10°C |
| 1 | 凉 | 10 ≤ HI < 20°C |
| 2 | 舒适 | 20 ≤ HI < 28°C |
| 3 | 热 | 28 ≤ HI < 35°C |
| 4 | 酷热 | HI ≥ 35°C |

**边界守卫**：Steadman 公式仅在温度 ≥ 20°C 且湿度 ≥ 40% 时有效，超出范围直接以实测温度分级。

### 异常检测（algo/anomaly.c）

对 ADXL345 合加速度 `|a| = sqrt(x²+y²+z²)` 维护长度 8 的滑动窗口：

- **冷启动保护**：前 8 个样本为预热期，不触发告警（`history_count < HISTORY_LEN` 提前返回）
- **检测条件**：当前值偏离窗口均值超过 **0.3g** → `embedmq_post(EVT_ALERT_ANOMALY)`
- **UI 响应**：告警横幅显示 5 秒后自动消失；触摸屏任意点击提前 dismiss

---

## 开发阶段

### Phase 1 — 基础框架 ✅
- LVGL SDL2 模拟器跑通，显示静态界面
- embedmq 集成，post/register 流程验证
- 完整项目脚手架

### Phase 2 — 模拟器完善 ✅
- 5 路传感器 `#ifdef SIMULATOR` 随机游走数据
- 主循环忙等修复（`usleep` + `lv_timer_handler()` 返回值）
- 告警横幅 5 秒自动消失 + 触摸 dismiss
- `EVT_INPUT_TOUCH` 消费者链路补全（9 个 handler）
- 算法 bug 修复（anomaly 冷启动误报、comfort 公式边界守卫）

### Phase 3 — 数据层与网络 ✅
- SQLite WAL 持久化（storage/db.c，sqlite3 amalgamation 内嵌）
- MQTT 异步发布（network/mqtt_client.c，-DMQTT=ON 可选）
- 三 Tab UI（lv_tabview）+ 5 个 lv_chart 趋势折线图
- IR 遥控 Tab 切换（借用 touch_dirty 通道传递主线程信号）

### Phase 4 — 板子接入（需硬件）
- DHT11 字符设备驱动
- ADXL345 I2C（地址 0x53/0x1D，数据寄存器 0x32~0x37）
- SR501 / SR04 GPIO sysfs
- 光敏电阻 ADC sysfs
- LVGL framebuffer HAL（替换 SDL2）

### Phase 5 — 完善
- CJK 字体（lv_font_conv 生成 .c，放 fonts/）
- EEPROM 基线读写（storage/eeprom.c），异常检测对比持久化基线
