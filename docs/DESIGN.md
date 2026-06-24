# DESIGN.md — SenseFusion Panel 设计文档

## 数据结构

每个事件携带的 payload 定义在 `app/app_events.h`（以此为准，本文档保持同步）：

```c
// EVT_SENSOR_DHT11
typedef struct { float temperature; float humidity; } evt_dht11_t;

// EVT_SENSOR_ADXL345
typedef struct { float x; float y; float z; float magnitude; } evt_adxl345_t;

// EVT_SENSOR_SR501
typedef struct { uint8_t detected; } evt_sr501_t;

// EVT_SENSOR_SR04
typedef struct { float distance_cm; } evt_sr04_t;

// EVT_SENSOR_LIGHT
typedef struct { uint16_t lux; } evt_light_t;

// EVT_ALGO_COMFORT（由 on_dht11 触发舒适度计算后 post）
typedef struct { float heat_index; uint8_t level; } evt_comfort_t;

// EVT_ALERT_ANOMALY（由 on_adxl345 滑动窗口检测后 post）
typedef struct { uint8_t type; float magnitude; } evt_anomaly_t;

// EVT_INPUT_TOUCH
typedef struct { int32_t x; int32_t y; uint8_t pressed; } evt_touch_t;

// EVT_INPUT_IR
typedef struct { uint16_t key_code; } evt_ir_t;
```

---

## 线程安全设计

LVGL 使用 `LV_USE_OS = LV_OS_NONE`，不是线程安全的。

**规则：**
- embedmq 消费者线程（`ui_on_*` 回调）只写 `sensor_cache_t`，加 mutex
- 所有 `lv_*` API 只在主线程的 `dashboard_tick()` 里调用
- `dashboard_tick()` 结尾调用 `lv_timer_handler()` 并返回其结果（ms）
- 主循环用返回值 `usleep()` 休眠，避免 CPU 空转

```
embedmq 消费者线程          主线程
       │                      │
  ui_on_dht11()          dashboard_tick()
       │                      │
  mutex_lock()           mutex_lock()
  cache.temp = x         local = cache
  cache.dirty = true     cache.dirty = false
  mutex_unlock()         mutex_unlock()
                              │
                         lv_label_set_text(...)
                         lv_timer_handler() → wait_ms
                              │
                         usleep(wait_ms)
```

---

## embedmq 单处理器约束

`embedmq_register` 对同一 UUID 只接受一个处理器。需要多个模块响应同一事件时，在处理器内链式调用：

```c
void ui_on_dht11(const void *payload, size_t size, void *ctx) {
    const evt_dht11_t *ev = payload;
    dashboard_update_dht11(ev->temperature, ev->humidity);  // 写 UI 缓存
    algo_comfort_on_dht11(payload, size, NULL);              // 触发算法
}
// algo_comfort_on_dht11 计算完后再 post(EVT_ALGO_COMFORT)
// ui_on_comfort 回调更新舒适度卡片
```

---

## 算法模块

### 舒适度指数（algo/comfort_index.c）

基于 Steadman 热指数公式，融合温度和湿度计算体感温度（heat index），映射为 5 个等级。
公式适用范围：温度 ≥ 20°C 且湿度 ≥ 40%；超出范围时以实际温度直接分级。

| level | 描述 | 触发条件（参考） |
|---|---|---|
| 0 | 冷 | HI < 10°C |
| 1 | 凉 | 10 ≤ HI < 20°C |
| 2 | 舒适 | 20 ≤ HI < 28°C |
| 3 | 热 | 28 ≤ HI < 35°C |
| 4 | 酷热 | HI ≥ 35°C |

### 异常检测（algo/anomaly.c）

对 ADXL345 的合加速度 `|a| = sqrt(x²+y²+z²)` 做滑动窗口（8 个样本）均值。
窗口填满（前 8 个样本为预热期）后，若当前值偏离均值超过 0.3g，post `EVT_ALERT_ANOMALY`，触发 UI 告警横幅。
告警横幅显示约 5 秒后自动消失；触摸屏点击任意位置可提前 dismiss。

---

## 开发阶段规划

### Phase 1 — 基础框架 ✅
- LVGL SDL2 模拟器跑通，显示静态界面
- embedmq 集成，测试 post/register 流程
- 完整项目脚手架

### Phase 2 — 模拟器完善 ✅
- 所有传感器线程 `#ifdef SIMULATOR` 随机游走数据
- 主循环忙等修复（`usleep` + `lv_timer_handler` 返回值）
- 告警横幅自动消失 + 触摸 dismiss
- `EVT_INPUT_TOUCH` 消费者链路补全
- 算法层 bug 修复（anomaly 懒初始化、comfort 公式边界守卫）

### Phase 3 — 数据层与网络 ✅

- SQLite 数据库（storage/db.c）：WAL 模式，embedmq 消费者线程写入，每次传感器更新记一条
- MQTT 发布（network/mqtt_client.c）：libmosquitto 异步模式，可选编译（-DMQTT=ON）
- 三 Tab UI（总览 / 趋势 / 设置）：lv_tabview，IR 遥控左右切换
- 趋势页：5 个 lv_chart（温度 / 湿度 / 距离 / 光照 / 加速度幅值），CHART_POINTS=60 滚动显示
- SQLite amalgamation 内嵌（third_party/sqlite3/），不依赖系统 libsqlite3，交叉编译友好

### Phase 4 — 板子接入（需硬件）
- DHT11 字符设备驱动
- 光敏 ADC sysfs
- SR501 / SR04 GPIO sysfs
- ADXL345 I2C（地址 0x53 或 0x1D，寄存器 0x32~0x37）
- LVGL framebuffer HAL（替换 SDL2）

### Phase 4 — 算法与存储
- EEPROM 基线读写（storage/eeprom.c）
- 异常检测对比 EEPROM 基线

### Phase 5 — 完善
- CJK 字体（lv_font_conv 生成，放 fonts/）
- IR 遥控翻页控制
