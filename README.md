# SenseFusion Panel

基于 IMX6ULL 的多传感器融合显示终端。通过 [embedmq](https://github.com/w4ysonch/embedmq) 消息总线串联传感器采集、算法处理与 LVGL UI 三层，在 800×480 触摸屏上实时展示环境感知数据。

## 硬件

| 模块 | 接口 | 说明 |
|---|---|---|
| IMX6ULL 核心板 | — | 主控，Linux |
| DHT11 | 单总线（字符设备） | 温湿度 |
| ADXL345 | I2C | 三轴加速度 |
| SR501 | GPIO | 人体感应 |
| SR04 | GPIO（trigger/echo） | 超声波距离 |
| 光敏电阻 | ADC sysfs | 光照强度 |
| 触摸屏 | `/dev/input/event*` | 多点触控 |
| 红外遥控 | `/dev/input/event*` | 按键输入 |

## 架构

```
                    ┌─────────────────┐
                    │     main.c      │
                    │  初始化 + 启动   │
                    └────────┬────────┘
                             │
         ┌───────────────────┼───────────────────┐
         │                   │                   │
    传感器线程组          输入线程组           LVGL 主线程
  ┌──────────────┐    ┌─────────────┐    ┌──────────────────┐
  │ dht11_thread │    │touch_thread │    │ dashboard_tick() │
  │ adxl_thread  │    │  ir_thread  │    │ lv_timer_handler │
  │ sr501_thread │    └──────┬──────┘    └────────▲─────────┘
  │ sr04_thread  │           │                    │
  │ light_thread │           │            写 sensor_cache
  └──────┬───────┘           │            （mutex 保护）
         │ embedmq_post()    │                    │
         └───────────┬───────┘                    │
                     ▼                            │
              ┌────────────┐                      │
              │  embedmq   │                      │
              │ 消费者线程  │                      │
              └─────┬──────┘                      │
                    │ 调用注册的 handler            │
          ┌─────────┼──────────┐                  │
          ▼         ▼          ▼                  │
     ui_on_dht11  ui_on_accel  ...  ──────────────┘
          │
          ▼
     algo/comfort_index
     algo/anomaly
          │
          ▼ embedmq_post(EVT_ALGO_*)
     ui_on_comfort / ui_on_anomaly
```

传感器线程只管 post，不碰 LVGL；embedmq 回调只写缓存；所有 LVGL 调用集中在主线程的 `dashboard_tick()`。详见 [docs/DESIGN.md](docs/DESIGN.md)。

## 事件总线

| 事件 | payload | 发送方 |
|---|---|---|
| `EVT_SENSOR_DHT11` | `{ temperature, humidity }` | sensor_dht11 |
| `EVT_SENSOR_ADXL345` | `{ x, y, z, magnitude }` | sensor_adxl345 |
| `EVT_SENSOR_SR501` | `{ detected }` | sensor_sr501 |
| `EVT_SENSOR_SR04` | `{ distance_cm }` | sensor_sr04 |
| `EVT_SENSOR_LIGHT` | `{ lux }` | sensor_light |
| `EVT_ALGO_COMFORT` | `{ score, level }` | algo/comfort_index |
| `EVT_ALGO_ANOMALY` | `{ source, value, baseline }` | algo/anomaly |
| `EVT_INPUT_TOUCH` | `{ x, y }` | input_touch |
| `EVT_INPUT_IR` | `{ key_code }` | input_ir |

## 编译

### 依赖

```bash
# PC 模拟器模式
sudo apt install cmake gcc g++ libsdl2-dev

# 板子交叉编译
# 需要 arm-linux-gnueabihf 工具链
```

### PC 模拟器（SDL2 窗口，无需板子）

```bash
git clone --recurse-submodules https://github.com/w4ysonch/sensefusion-panel.git
cd sensefusion-panel
mkdir build && cd build
cmake .. -DSIMULATOR=ON
make -j$(nproc)
./sensefusion-panel
```

### IMX6ULL 交叉编译

```bash
mkdir build-arm && cd build-arm
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-linux-gnueabihf.cmake
make -j$(nproc)
# 将产物 scp 到板子后运行
```

## 目录结构

```
sensefusion-panel/
├── main.c                  主入口，启动线程，驱动 LVGL 主循环
├── app/                    消息总线初始化，事件定义
├── sensors/                5 路传感器采集线程（板子上为实际驱动，TODO）
├── input/                  触摸屏 / 红外遥控输入线程
├── algo/                   舒适度指数计算、加速度异常检测
├── ui/                     LVGL 控件更新、事件处理器
├── storage/                EEPROM 掉电保存（TODO）
├── sim/                    PC 模拟器 SDL2 HAL
├── fonts/                  自定义 LVGL 字体文件（CJK 待添加）
├── third_party/
│   ├── embedmq/            消息总线库（git submodule）
│   ├── lvgl/               LVGL v9（git submodule）
│   └── lv_conf.h           LVGL 配置
├── cmake/                  交叉编译 toolchain（TODO）
└── docs/                   设计文档
```

## 当前状态

**Phase 3 完成，数据层与网络已就绪：**

- [x] PC 模拟器可运行，深色主题 800×480 三 Tab UI（总览 / 趋势 / 设置）
- [x] embedmq 事件链路：传感器 → UI → 算法 → UI（9 个处理器）
- [x] 模拟器随机游走数据（5 路传感器实时动态变化）
- [x] 趋势页：5 个 lv_chart 折线图（60 点滚动，温湿度 / 距离 / 光照 / 加速度幅值）
- [x] SQLite 数据持久化（WAL 模式，每次传感器更新写入）
- [x] SQLite amalgamation 内嵌（third_party/sqlite3/），交叉编译不依赖系统库
- [x] MQTT 可选发布（-DMQTT=ON，libmosquitto 异步，主题 sensefusion/\<sensor\>）
- [x] IR 遥控 Tab 切换（KEY_LEFT/RIGHT 循环切换三页面）
- [x] 异常检测告警横幅 5 秒自动消失 / 触摸提前 dismiss
- [x] 交叉编译 toolchain 骨架（cmake/arm-linux-gnueabihf.cmake）
- [ ] 传感器驱动（板子上待实现，见 sensors/ 各 TODO 注释）
- [ ] LVGL framebuffer HAL（板子上待实现）
- [ ] CJK 字体（中文暂显示为方块）
- [ ] EEPROM 存储

## License

MIT © 2026 w4ysonch
