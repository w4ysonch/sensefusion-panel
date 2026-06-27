# SenseFusion Panel

**多模态传感融合边缘终端** — 基于 IMX6ULL Linux 的嵌入式感知系统。

自研 [embedmq](https://github.com/w4ysonch/embedmq) 消息总线将 5 路异构传感器、算法层、SQLite 持久化、MQTT 上报与 LVGL 可视化解耦串联，在 800×480 触摸屏上提供实时数据总览、历史趋势图表与远程监控能力。

## 硬件

| 模块 | 接口 | 用途 |
|---|---|---|
| IMX6ULL 核心板 | — | 主控，Linux |
| DHT11 | 单总线（字符设备） | 温湿度采集 |
| ADXL345 | I2C | 三轴加速度 / 冲击检测 |
| SR501 | GPIO | 人体红外感应 |
| SR04 | GPIO（trigger/echo） | 超声波测距 |
| 光敏电阻 | ADC sysfs | 环境光照 |
| 触摸屏 | `/dev/input/event*` MT-B | 多点触控 |
| 红外遥控 | `/dev/input/event*` | Tab 页切换 |

## 架构

```
               ┌──────────────────────────────────────────────────┐
               │                    main.c                        │
               │   app_init / db_init / mqtt_init / 启动线程      │
               └───────────────────────┬──────────────────────────┘
                                       │
          ┌────────────────────────────┼───────────────────┐
          │                            │                   │
     传感器线程组                   输入线程组          LVGL 主线程
 ┌───────────────┐             ┌──────────────┐   ┌─────────────────────┐
 │ dht11_thread  │             │ touch_thread │   │   dashboard_tick()  │
 │ adxl_thread   │             │   ir_thread  │   │  lv_timer_handler() │
 │ sr501_thread  │             └──────┬───────┘   └──────────▲──────────┘
 │ sr04_thread   │                    │                      │
 │ light_thread  │                    │           读 sensor_cache（mutex）
 └──────┬────────┘                    │                      │
        │ embedmq_post()              │                      │
        └────────────────┬────────────┘                      │
                         ▼                                    │
                  ┌────────────┐                              │
                  │  embedmq   │                              │
                  │ 消费者线程  │                              │
                  └─────┬──────┘                              │
                        │ ui_on_*(payload, size, ctx)         │
                        ├── dashboard_update_*() ─────────────┘  写缓存
                        ├── algo_comfort / algo_anomaly           触发算法
                        ├── db_log_*()                            SQLite 持久化
                        └── mqtt_publish_*()                      MQTT 上报
```

传感器线程只 `embedmq_post`，不接触 LVGL；消费者线程只写缓存、写 DB、发 MQTT；所有 `lv_*` API 集中在主线程 `dashboard_tick()`。详见 [docs/DESIGN.md](docs/DESIGN.md)。

## 事件总线

| 事件 | Payload | 发布方 |
|---|---|---|
| `EVT_SENSOR_DHT11` | `{ float temperature; float humidity; }` | sensor_dht11 |
| `EVT_SENSOR_ADXL345` | `{ float x, y, z, magnitude; }` | sensor_adxl345 |
| `EVT_SENSOR_SR501` | `{ uint8_t detected; }` | sensor_sr501 |
| `EVT_SENSOR_SR04` | `{ float distance_cm; }` | sensor_sr04 |
| `EVT_SENSOR_LIGHT` | `{ uint16_t lux; }` | sensor_light |
| `EVT_ALGO_COMFORT` | `{ float heat_index; uint8_t level; }` | algo/comfort_index |
| `EVT_ALERT_ANOMALY` | `{ uint8_t type; float magnitude; }` | algo/anomaly |
| `EVT_INPUT_TOUCH` | `{ int32_t x, y; uint8_t pressed; }` | input_touch |
| `EVT_INPUT_IR` | `{ uint16_t key_code; }` | input_ir |

## 编译

### 依赖

```bash
# PC 模拟器（SQLite 已内嵌，无需额外安装）
sudo apt install cmake gcc g++ libsdl2-dev

# 可选：MQTT 支持
sudo apt install libmosquitto-dev

# 板子交叉编译
# 需要 100ask arm-buildroot-linux-gnueabihf 工具链
```

### PC 模拟器

```bash
git clone --recurse-submodules https://github.com/w4ysonch/sensefusion-panel.git
cd sensefusion-panel
mkdir build && cd build

cmake .. -DSIMULATOR=ON                        # 基础模式
cmake .. -DSIMULATOR=ON -DMQTT=ON              # 启用 MQTT 发布
cmake .. -DSIMULATOR=ON -DASAN=ON              # 启用 AddressSanitizer

make -j$(nproc)
./sensefusion-panel
```

### IMX6ULL 交叉编译

```bash
mkdir build-arm && cd build-arm
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-linux-gnueabihf.cmake
make -j$(nproc)
# scp sensefusion-panel root@<board-ip>:/opt/
```

## 目录结构

```
sensefusion-panel/
├── main.c                  主入口：初始化、启动线程、LVGL 主循环
├── app/
│   ├── app_events.h        所有事件宏（EVT_*）及 payload 结构体
│   └── app_init.c/h        embedmq 实例创建、9 个 handler 注册、teardown
├── sensors/                5 路传感器采集线程（板子驱动 TODO，simulator 随机游走）
├── input/                  触摸屏（MT-B）与红外遥控输入线程
├── algo/
│   ├── comfort_index.c/h   Steadman 热指数 → 5 级舒适度
│   └── anomaly.c/h         ADXL345 滑动窗口（8 样本）异常检测
├── ui/
│   ├── ui_handlers.c/h     embedmq 回调：更新缓存 + 触发算法 + db/mqtt
│   └── ui_dashboard.c/h    LVGL 三 Tab UI、sensor_cache_t、dashboard_tick()
├── storage/
│   ├── db.c/h              SQLite WAL 日志（每次传感器更新写入）
│   └── eeprom.c/h          I2C EEPROM 持久化（TODO）
├── network/
│   └── mqtt_client.c/h     libmosquitto 异步发布，-DMQTT=ON 启用
├── sim/
│   └── lv_drv_sdl.c/h      PC 模拟器 SDL2 HAL（#ifdef SIMULATOR）
├── fonts/                  LVGL 自定义字体（CJK 待生成）
├── third_party/
│   ├── embedmq/            消息总线（git submodule）
│   ├── lvgl/               LVGL v9（git submodule）
│   ├── sqlite3/            SQLite 3.47.2 amalgamation（内嵌，无系统依赖）
│   └── lv_conf.h           LVGL 配置
├── cmake/
│   └── arm-linux-gnueabihf.cmake   Cortex-A7 交叉编译 toolchain
└── docs/
    └── DESIGN.md           架构与设计决策文档
```

## MQTT 主题

启用 `-DMQTT=ON` 后，各传感器数据发布至：

| 主题 | JSON 示例 |
|---|---|
| `sensefusion/dht11` | `{"ts":1700000000,"temp":26.3,"humi":58.0}` |
| `sensefusion/adxl345` | `{"ts":...,"x":0.01,"y":-0.02,"z":0.99,"mag":0.99}` |
| `sensefusion/sr501` | `{"ts":...,"detected":1}` |
| `sensefusion/sr04` | `{"ts":...,"dist_cm":45.2}` |
| `sensefusion/light` | `{"ts":...,"lux":350}` |
| `sensefusion/comfort` | `{"ts":...,"heat_index":28.1,"level":3}` |
| `sensefusion/anomaly` | `{"ts":...,"type":1,"magnitude":1.52}` |

## 当前状态

**Phase 3 完成，模拟器完整可运行：**

- [x] 三 Tab UI（总览 / 趋势 / 设置），深色主题 800×480
- [x] 5 路传感器模拟器随机游走数据，实时驱动所有图表
- [x] 趋势页 5 个 lv_chart 折线图（60 点滚动窗口）
- [x] SQLite WAL 持久化，sqlite3 amalgamation 内嵌，交叉编译无额外依赖
- [x] MQTT 异步发布（-DMQTT=ON，libmosquitto，主题前缀 `sensefusion/`）
- [x] ADXL345 异常检测告警横幅（5 秒自动消失 / 触摸提前 dismiss）
- [x] IR 遥控 Tab 切换（KEY_LEFT/KEY_RIGHT）
- [x] Cortex-A7 交叉编译 toolchain（Buildroot SDK，已可编译）
- [ ] 板子传感器驱动（sensors/ 各 `#else` 分支，TODO）
- [ ] LVGL framebuffer HAL（替换 SDL2，TODO）
- [ ] CJK 字体（中文暂显示为方块）
- [ ] EEPROM 基线存储

## License

MIT © 2026 w4ysonch
