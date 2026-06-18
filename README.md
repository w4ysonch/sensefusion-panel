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
│   ├── lvgl/               LVGL v9（直接 clone）
│   └── lv_conf.h           LVGL 配置
└── cmake/                  交叉编译 toolchain（TODO）
```

## 架构

```
传感器线程  ──embedmq_post──►  embedmq 消费者线程
                                      │
                              ui_handlers（embedmq 回调）
                             ┌────────┴────────┐
                      写 sensor_cache      调用 algo 模块
                             │                  │
                        主线程 dashboard_tick()  └─► embedmq_post 算法结果
                             │
                        lv_timer_handler()
```

传感器线程与 LVGL 主线程通过 `sensor_cache_t`（mutex 保护）解耦，embedmq 消费者线程只写缓存，LVGL 调用只在主线程。

## 当前状态

- [x] PC 模拟器可运行，深色主题 800×480 Dashboard
- [x] embedmq 事件链路：传感器 → UI → 算法 → UI
- [ ] 传感器驱动（板子上待实现）
- [ ] LVGL framebuffer HAL（板子上待实现）
- [ ] CJK 字体（中文暂显示为方块）
- [ ] EEPROM 存储
- [ ] 交叉编译 toolchain 文件
