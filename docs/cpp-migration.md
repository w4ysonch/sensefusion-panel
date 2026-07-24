# sensefusion-panel C++ 迁移指南

## 迁移原则

- **上层业务逻辑改 C++**：algo、storage、daemon、sensors、input、network、入口文件
- **底层协议和三方库保持 C**：ipc_protocol.h 的 POD 结构体、embedmq、lvgl、sqlite3
- **IPC 层（ipc_socket/ipc_mq/ipc_shm）保持 C**，头文件加 `extern "C"` 即可从 C++ 调用
- embedmq 的回调签名 `void(const void *, size_t, void *)` 是 C ABI，不能改，所有 handler 保持此签名

## 通用改法（适用所有文件）

1. 文件后缀 `.c` → `.cpp`（CMakeLists.txt 对应更新）
2. C-style cast 全部替换：
   - `(SomeType *)ptr` → `static_cast<SomeType *>(ptr)`
   - `(char *)ptr` / `(const char *)ptr` → `reinterpret_cast<char *>(ptr)`
   - sockaddr 转换 `(struct sockaddr *)&addr` → `reinterpret_cast<sockaddr *>(&addr)`
3. `static volatile int g_running` → `std::atomic<bool> g_running{true}`
4. `pthread_t` + `pthread_create` → `std::thread`（注意：`pthread_cancel` 无对应，见下文）
5. C99 designated initializer（`.field = val`）在 C++ 中需要改成位置初始化或 C++20 designated init
6. `{0}` 结构体零初始化 → `{}`

---

## 一、algo/

### 1.1 comfort_index.h → comfort_index.hpp

原文件：
```c
#pragma once
#include <stdint.h>
#include "../third_party/embedmq/include/embedmq.h"

typedef enum {
    COMFORT_COOL    = 0,
    COMFORT_NORMAL  = 1,
    COMFORT_WARM    = 2,
    COMFORT_HOT     = 3,
    COMFORT_EXTREME = 4,
} comfort_level_t;

void algo_comfort_on_dht11(const void *payload, size_t size, void *ctx);
```

改后：
```cpp
#pragma once
#include <cstdint>
#include "../third_party/embedmq/include/embedmq.h"

enum class ComfortLevel : uint8_t {
    Cool    = 0,
    Normal  = 1,
    Warm    = 2,
    Hot     = 3,
    Extreme = 4,
};

// 保持 C ABI 签名供 embedmq 注册
extern "C" void algo_comfort_on_dht11(const void *payload, size_t size, void *ctx);
```

**注意**：`comfort_level_t` 改成 `enum class ComfortLevel` 后，`ui_handlers.cpp` 里的
`static_cast<ComfortLevel>(ev->level)` 要对应修改，`evt_comfort_t` 里的 `level` 字段类型也要改成 `uint8_t`（保持 POD，不能在 ipc_protocol.h 里用 enum class）。

### 1.2 comfort_index.c → comfort_index.cpp

原文件关键部分：
```c
static float heat_index(float t, float h) { ... }
static comfort_level_t classify(float hi) { ... }

void algo_comfort_on_dht11(const void *payload, size_t size, void *ctx)
{
    (void)ctx; (void)size;
    const evt_dht11_t *ev = (const evt_dht11_t *)payload;
    float hi = heat_index(ev->temperature, ev->humidity);
    comfort_level_t level = classify(hi);
    evt_comfort_t out = { .heat_index = hi, .level = (uint8_t)level };
    embedmq_post(g_mq, EVT_ALGO_COMFORT, &out, sizeof(out));
}
```

改后：
```cpp
#include "comfort_index.hpp"
#include "../common/app_common.h"
#include <cmath>

static float heat_index(float t, float h) { /* 同原来，不变 */ }

static ComfortLevel classify(float hi)
{
    if (hi < 27.0f) return ComfortLevel::Cool;
    if (hi < 32.0f) return ComfortLevel::Normal;
    if (hi < 41.0f) return ComfortLevel::Warm;
    if (hi < 54.0f) return ComfortLevel::Hot;
    return ComfortLevel::Extreme;
}

extern "C" void algo_comfort_on_dht11(const void *payload, size_t size, void *ctx)
{
    (void)ctx; (void)size;
    const auto *ev = static_cast<const evt_dht11_t *>(payload);
    float hi = heat_index(ev->temperature, ev->humidity);
    ComfortLevel level = classify(hi);
    evt_comfort_t out{};
    out.heat_index = hi;
    out.level = static_cast<uint8_t>(level);
    embedmq_post(g_mq, EVT_ALGO_COMFORT, &out, sizeof(out));
}
```

### 1.3 anomaly.h → anomaly.hpp

原文件：
```c
#pragma once
#include <stddef.h>
void algo_anomaly_on_adxl345(const void *payload, size_t size, void *ctx);
void algo_anomaly_set_threshold(float threshold);
```

改后：
```cpp
#pragma once
#include <cstddef>
#include <atomic>

extern "C" void algo_anomaly_on_adxl345(const void *payload, size_t size, void *ctx);
void algo_anomaly_set_threshold(float threshold);  // 内部用 std::atomic<float>
```

### 1.4 anomaly.c → anomaly.cpp

原文件关键部分：
```c
#define WINDOW_SIZE 8
static float s_history[WINDOW_SIZE];
static int   s_idx      = 0;
static int   s_count    = 0;
static float s_threshold = 2.0f;

void algo_anomaly_set_threshold(float threshold)
{
    s_threshold = threshold;
}

void algo_anomaly_on_adxl345(const void *payload, size_t size, void *ctx)
{
    (void)ctx; (void)size;
    const evt_adxl345_t *ev = (const evt_adxl345_t *)payload;
    ...
    if (magnitude > s_threshold) { ... }
}
```

改后：
```cpp
#include "anomaly.hpp"
#include "../common/app_common.h"
#include <atomic>
#include <cmath>

static constexpr int kWindowSize = 8;
static float s_history[kWindowSize];
static int   s_idx   = 0;
static int   s_count = 0;
static std::atomic<float> s_threshold{2.0f};  // 原 static float，改成 atomic

void algo_anomaly_set_threshold(float threshold)
{
    s_threshold.store(threshold, std::memory_order_relaxed);
}

extern "C" void algo_anomaly_on_adxl345(const void *payload, size_t size, void *ctx)
{
    (void)ctx; (void)size;
    const auto *ev = static_cast<const evt_adxl345_t *>(payload);
    // 其余逻辑不变，读阈值改为：
    float thr = s_threshold.load(std::memory_order_relaxed);
    if (magnitude > thr) { /* 发告警，同原来 */ }
}
```

---

## 二、storage/

### 2.1 settings.h → settings.hpp

原文件：
```c
#pragma once
#include <stdint.h>

#define SETTINGS_MAGIC             0xA55A
#define SETTINGS_DEFAULT_THRESHOLD 2.0f

typedef struct {
    float    anomaly_threshold;
    uint16_t magic;
} app_settings_t;

void settings_load(app_settings_t *s);
void settings_save(const app_settings_t *s);
```

改后：
```cpp
#pragma once
#include <cstdint>
#include <optional>

inline constexpr uint16_t kSettingsMagic             = 0xA55A;
inline constexpr float    kSettingsDefaultThreshold  = 2.0f;

// 保持 POD，跨进程共享内存用，不加构造函数
struct AppSettings {
    float    anomaly_threshold;
    uint16_t magic;
};

std::optional<AppSettings> settings_load();
void settings_save(const AppSettings &s);
```

### 2.2 settings.c → settings.cpp

原文件：
```c
#include "settings.h"
#include "eeprom.h"
#include <string.h>

static const app_settings_t DEFAULT_SETTINGS = {
    .anomaly_threshold = SETTINGS_DEFAULT_THRESHOLD,
    .magic             = SETTINGS_MAGIC,
};

void settings_load(app_settings_t *s)
{
    eeprom_read(0, s, sizeof(*s));
    if (s->magic != SETTINGS_MAGIC)
        *s = DEFAULT_SETTINGS;
}

void settings_save(const app_settings_t *s)
{
    eeprom_write(0, s, sizeof(*s));
}
```

改后：
```cpp
#include "settings.hpp"
#include "eeprom.h"
#include <optional>

static constexpr AppSettings kDefaultSettings{
    kSettingsDefaultThreshold,  // anomaly_threshold
    kSettingsMagic,             // magic
};

std::optional<AppSettings> settings_load()
{
    AppSettings s{};
    eeprom_read(0, &s, sizeof(s));
    if (s.magic != kSettingsMagic)
        return kDefaultSettings;
    return s;
}

void settings_save(const AppSettings &s)
{
    eeprom_write(0, &s, sizeof(s));
}
```

调用方改为：
```cpp
auto settings = settings_load().value_or(AppSettings{kSettingsDefaultThreshold, kSettingsMagic});
```

### 2.3 db.h → db.hpp / db.c → db.cpp

SQLite 句柄 `static sqlite3 *s_db` 是裸指针，`db_init`/`db_deinit` 手动管生命周期，改成 RAII 单例：

```cpp
// db.hpp
#pragma once
#include <string_view>

class Database {
public:
    static Database& instance();
    bool init(std::string_view path);
    void deinit();
    void log_dht11(float temp, float humi);
    void log_adxl345(float x, float y, float z, float mag);
    void log_sr501(int detected);
    void log_sr04(float dist);
    void log_light(float lux);
    void cleanup_old(int days);
private:
    Database() = default;
    struct sqlite3 *db_{nullptr};
};

// 兼容原有 C 风格调用（sensor_daemon.cpp 里可以继续用）
inline bool  db_init(const char *path)  { return Database::instance().init(path); }
inline void  db_deinit()                { Database::instance().deinit(); }
inline void  db_cleanup_old(int days)   { Database::instance().cleanup_old(days); }
inline void  db_log_dht11(float t, float h)  { Database::instance().log_dht11(t, h); }
inline void  db_log_adxl345(float x, float y, float z, float m) { Database::instance().log_adxl345(x,y,z,m); }
inline void  db_log_sr501(int d)        { Database::instance().log_sr501(d); }
inline void  db_log_sr04(float d)       { Database::instance().log_sr04(d); }
inline void  db_log_light(float l)      { Database::instance().log_light(l); }
```

db.cpp 内部：`deinit()` 调 `sqlite3_close(db_); db_ = nullptr;`，SQL 逻辑不变，只把 `sprintf` 改 `snprintf`，C-style cast 改 `reinterpret_cast`。

### 2.4 eeprom.h / eeprom.c — 保持 C，不改

从 C++ 文件调用时在 include 前加：
```cpp
extern "C" {
#include "storage/eeprom.h"
}
```

---

## 三、daemon/daemon_handlers.c → daemon_handlers.cpp

### 3.1 daemon_handlers.hpp

```cpp
#pragma once
#include <mqueue.h>

void daemon_handlers_set_ipc(int sock_fd, mqd_t alert_mq);

extern "C" {
void daemon_on_dht11   (const void *p, size_t sz, void *ctx);
void daemon_on_adxl345 (const void *p, size_t sz, void *ctx);
void daemon_on_sr501   (const void *p, size_t sz, void *ctx);
void daemon_on_sr04    (const void *p, size_t sz, void *ctx);
void daemon_on_light   (const void *p, size_t sz, void *ctx);
void daemon_on_comfort (const void *p, size_t sz, void *ctx);
void daemon_on_anomaly (const void *p, size_t sz, void *ctx);
}
```

### 3.2 daemon_handlers.cpp

改动清单：
- 所有 `(const evt_xxx_t *)payload` → `static_cast<const evt_xxx_t *>(payload)`
- `ipc_frame_t f = {0}` → `ipc_frame_t f{}`
- `(mqd_t)-1` → `static_cast<mqd_t>(-1)`
- `NULL` → `nullptr`
- 函数定义前加 `extern "C"`

示例（daemon_on_dht11）：
```cpp
extern "C" void daemon_on_dht11(const void *payload, size_t size, void *ctx)
{
    (void)ctx; (void)size;
    const auto *ev = static_cast<const evt_dht11_t *>(payload);
    ipc_frame_t f{};
    f.type              = IPC_MSG_DHT11;
    f.dht11.temperature = ev->temperature;
    f.dht11.humidity    = ev->humidity;
    ipc_socket_send(s_sock_fd, &f);
    algo_comfort_on_dht11(payload, size, nullptr);
    db_log_dht11(ev->temperature, ev->humidity);
    mqtt_publish_dht11(ev->temperature, ev->humidity);
}
```

其余 6 个 handler 结构完全一样，只改 cast 和 NULL→nullptr。

---

## 四、sensors/ — 统一改 std::thread

所有传感器文件（`sensor_dht11.c`、`sensor_adxl345.c`、`sensor_sr501.c`、`sensor_sr04.c`、`sensor_light.c`）结构完全一样，改法统一：

原文件（以 sensor_dht11.c 为例）：
```c
void *sensor_dht11_thread(void *arg)
{
    (void)arg;
    uint32_t uuid = embedmq_uuid(EVT_SENSOR_DHT11);
    while (1) {
        evt_dht11_t ev;
        if (read_dht11(&ev.temperature, &ev.humidity) == 0)
            embedmq_post_id(g_mq, uuid, &ev, sizeof(ev));
        sleep(2);
    }
    return NULL;
}
```

改成 `.cpp` 后：
```cpp
// 线程函数本体不变，但改用 lambda 包装给 std::thread
// 在 sensor_daemon.cpp 里启动方式改为（见第六节）
// std::thread(sensor_dht11_thread, nullptr)
// 或者直接把 while 循环写成 lambda
```

**注意**：`pthread_cancel` 没有 `std::thread` 对应，需要在线程循环里加退出标志：
```cpp
// 每个传感器线程内部
extern std::atomic<bool> g_running;  // 由 sensor_daemon.cpp 定义

void *sensor_dht11_thread(void *arg)
{
    (void)arg;
    uint32_t uuid = embedmq_uuid(EVT_SENSOR_DHT11);
    while (g_running.load()) {   // 改为检查 atomic flag
        evt_dht11_t ev;
        if (read_dht11(&ev.temperature, &ev.humidity) == 0)
            embedmq_post_id(g_mq, uuid, &ev, sizeof(ev));
        sleep(2);
    }
    return nullptr;
}
```

---

## 五、input/

### input_touch.c → input_touch.cpp
### input_ir.c → input_ir.cpp

结构与 sensors 完全一样，改动也相同：
- `void *` 返回改 `nullptr`
- `(void)arg` 保持不变
- `while(1)` 改 `while(g_running.load())`
- 去掉 `pthread_cancel` 依赖，用 `g_running` 协同退出

---

## 六、network/mqtt_client.c → mqtt_client.cpp

libmosquitto 是 C 库，头文件包含时加 `extern "C"`：
```cpp
extern "C" {
#include <mosquitto.h>
}
```

原文件中的改动：
- `static struct mosquitto *s_mosq = NULL` → `static struct mosquitto *s_mosq = nullptr`
- `(void *)` 转换 → `static_cast<void *>(...)`
- 回调函数签名由 libmosquitto 决定，保持不变

---

## 七、sensor_daemon.c → sensor_daemon.cpp

原文件：
```c
static volatile int g_running = 1;

static embedmq_config_t cfg = {
    .queue_size   = 4096,
    .max_msg_size = 64,
    .max_handlers = 16,
};

int main(void)
{
    ...
    pthread_t threads[5];
    pthread_create(&threads[0], NULL, sensor_dht11_thread, NULL);
    ...
    for (int i = 0; i < 5; i++)
        pthread_cancel(threads[i]);
    for (int i = 0; i < 5; i++)
        pthread_join(threads[i], NULL);
}
```

改后：
```cpp
#include <atomic>
#include <thread>
#include <vector>

std::atomic<bool> g_running{true};  // sensors/ 里 extern 这个

static void on_signal(int) { g_running.store(false); }

int main()
{
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    // designated initializer 在 C++20 合法，C++17 改成位置初始化：
    embedmq_config_t cfg{4096, 64, 16};  // queue_size, max_msg_size, max_handlers
    g_mq = embedmq_create(&cfg);

    ...

    // pthread_t 数组改 std::vector<std::thread>
    std::vector<std::thread> threads;
    threads.emplace_back(sensor_dht11_thread,   nullptr);
    threads.emplace_back(sensor_adxl345_thread, nullptr);
    threads.emplace_back(sensor_sr501_thread,   nullptr);
    threads.emplace_back(sensor_sr04_thread,    nullptr);
    threads.emplace_back(sensor_light_thread,   nullptr);

    ...

    // 退出时：g_running 已经是 false，线程自己退出
    for (auto &t : threads)
        if (t.joinable()) t.join();
}
```

**注意**：`std::thread` 的线程函数签名要求是 `void()` 或能被调用的 callable，
但原来的传感器线程签名是 `void *(void *)` （pthread 要求）。
有两个办法：
1. 保留 `void *(void *)` 签名，用 `std::thread(sensor_dht11_thread, nullptr)` 直接传，std::thread 支持带参数的函数
2. 把线程函数改成 `void sensor_dht11_thread()` 无参版本，内部逻辑不变

推荐方案 2，更符合 C++ 风格。

---

## 八、ui_app.c → ui_app.cpp

改法与 `sensor_daemon.cpp` 完全对称：
- `static volatile int g_running` → `std::atomic<bool> g_running{true}`
- `embedmq_config_t cfg = { .field = val }` → `embedmq_config_t cfg{4096, 64, 16}`
- `pthread_t t_recv, t_alert, t_touch, t_ir` → `std::vector<std::thread>`
- `pthread_cancel` 全部删掉，依赖 `g_running` 协同退出
- `usleep((useconds_t)(wait_ms - 1) * 1000u)` → `usleep(static_cast<useconds_t>(wait_ms - 1) * 1000u)`
- `NULL` → `nullptr`

---

## 九、CMakeLists.txt 修改

把所有 `.c` 改成 `.cpp` 之后，CMakeLists.txt 里的 SOURCES 列表对应修改文件名。

C 标准可以保留（sqlite3、eeprom 等保持 C），C++ 标准从 C++14 升到 C++17（`std::optional` 需要 C++17）：

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

IPC 层头文件（`ipc_socket.h`、`ipc_mq.h`、`ipc_shm.h`）本身不改，但从 `.cpp` 文件 include 时需要加 `extern "C"`，或者在头文件里自己加：
```c
// 在 ipc_socket.h 顶部加
#ifdef __cplusplus
extern "C" {
#endif

// ... 原有内容 ...

#ifdef __cplusplus
}
#endif
```

这样 C 和 C++ 都可以直接 include，不用在调用方加。

---

## 十、改动优先级与顺序

按这个顺序改，每改一个模块就重新编译验证：

1. `algo/comfort_index` + `algo/anomaly` — 最独立，改完直接编译
2. `storage/settings` — 依赖 eeprom（保持C），改完验证 settings_load 返回 optional
3. `storage/db` — 改成 RAII 单例，inline 兼容函数保证调用方不用动
4. `daemon/daemon_handlers` — 依赖 algo + storage，改 cast + extern "C"
5. `sensors/` 五个文件 — 加 g_running 检查，改 nullptr
6. `input/` 两个文件 — 同 sensors
7. `network/mqtt_client` — 加 extern "C" 包 mosquitto.h
8. `sensor_daemon.c` → `sensor_daemon.cpp` — 改 atomic + std::thread
9. `ui_app.c` → `ui_app.cpp` — 同 sensor_daemon
10. CMakeLists.txt — 更新文件名，升 C++17，IPC 头文件加 extern "C" 守卫

---

## 十一、ui/ui_dashboard.c → ui_dashboard.cpp ✅ 已完成

### 目标

- 文件改 `.cpp`，机械替换让 C++ 占比达到 70%+
- 引入 `LvglMemberEventThunk` 模板，把现有的静态回调函数改成类成员函数绑定，这是真正有技术深度可以讲的部分

> **状态：已实现。** `LvglMemberEventThunk` 内联于 `ui/ui_dashboard.hpp`，Dashboard 单例于
> `ui/ui_dashboard.cpp`，7 个事件回调均改为成员函数。`ui_handlers.cpp` 调用
> `Dashboard::instance().update_*()`，`ui_ipc.cpp` 使用 `g_running.store(false)` 触发退出。

### 11.1 LvglMemberEventThunk 模板（新建 ui/lvgl_event_adapter.hpp）

参考 aipl-ui 的实现，新建一个小头文件：

```cpp
// ui/lvgl_event_adapter.hpp
#pragma once
#include "lvgl.h"

// 把成员函数指针编译期转换为 C 函数指针，零开销，无堆分配
// 用法：
//   lv_obj_add_event_cb(obj,
//       LvglMemberEventThunk<Dashboard, &Dashboard::OnBrightnessChanged>,
//       LV_EVENT_VALUE_CHANGED, this);
template <typename Receiver, void (Receiver::*Method)(lv_event_t *)>
void LvglMemberEventThunk(lv_event_t *e)
{
    auto *self = static_cast<Receiver *>(lv_event_get_user_data(e));
    if (self) (self->*Method)(e);
}

// lv_anim_t 的 exec_cb 版本（动画回调）
template <typename Receiver, void (Receiver::*Method)(lv_obj_t *, int32_t)>
void LvglMemberAnimThunk(void *obj, int32_t v)
{
    // 注意：anim exec_cb 没有 user_data，需要把 this 存在 lv_anim_t::user_data
    // 见 11.3 动画回调改法
}
```

### 11.2 Dashboard 类（ui_dashboard.hpp）

把现有的全局静态函数和全局变量包进一个类：

```cpp
// ui/ui_dashboard.hpp
#pragma once
#include "lvgl.h"
#include "../storage/settings.hpp"
#include "lvgl_event_adapter.hpp"

class Dashboard {
public:
    static Dashboard& instance();

    void init(const AppSettings &settings);
    uint32_t tick();

    // embedmq handler 调用的更新接口（保持不变）
    void update_dht11(float temp, float humi);
    void update_adxl345(float x, float y, float z, float mag);
    void update_sr501(int detected);
    void update_sr04(float dist_cm);
    void update_light(float lux);
    void update_comfort(float heat_index, uint8_t level);
    void show_alert(uint8_t type, float magnitude);

private:
    Dashboard() = default;

    // ── 设置页回调（原来的静态函数改成成员函数）────────────────
    void OnBrightnessChanged(lv_event_t *e);
    void OnUnitChanged(lv_event_t *e);
    void OnMuteChanged(lv_event_t *e);
    void OnThresholdChanged(lv_event_t *e);
    void OnDbCleanup(lv_event_t *e);
    void OnDetailClose(lv_event_t *e);
    void OnChartCardClick(lv_event_t *e);

    // ── 成员变量（原来的 g_xxx 全局变量）───────────────────────
    AppSettings settings_{};
    lv_obj_t *tabview_{nullptr};
    lv_obj_t *detail_panel_{nullptr};
    lv_obj_t *detail_chart_{nullptr};
    lv_obj_t *detail_title_{nullptr};
    // ... 其余 g_xxx 全部移进来，去掉 g_ 前缀，加 _ 后缀
};
```

### 11.3 回调改法（核心改动）

原来的静态函数写法：
```c
static void cb_brightness(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    g_settings.brightness = (uint8_t)lv_slider_get_value(sl);
    settings_save(&g_settings);
}

// 注册
lv_obj_add_event_cb(g_slider_brightness, cb_brightness,
                    LV_EVENT_VALUE_CHANGED, NULL);
```

改后成员函数写法：
```cpp
void Dashboard::OnBrightnessChanged(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    settings_.brightness = static_cast<uint8_t>(lv_slider_get_value(sl));
    settings_save(settings_);
}

// 注册（this 作为 user_data 传入）
lv_obj_add_event_cb(slider_brightness_,
    LvglMemberEventThunk<Dashboard, &Dashboard::OnBrightnessChanged>,
    LV_EVENT_VALUE_CHANGED, this);
```

需要这样改的回调共 7 个：
- `cb_brightness` → `OnBrightnessChanged`
- `cb_unit` → `OnUnitChanged`
- `cb_mute` → `OnMuteChanged`
- `cb_threshold` → `OnThresholdChanged`
- `cb_db_cleanup` → `OnDbCleanup`
- `detail_close_cb` → `OnDetailClose`
- `chart_card_click_cb` → `OnChartCardClick`

**注意**：`chart_card_click_cb` 原来用 `lv_event_get_user_data` 拿 `chart_meta_t *`，
改成成员函数后 `user_data` 被 `this` 占用了，`chart_meta_t` 改用 `lv_event_get_target`
配合成员变量里的 `chart_meta_t` 数组查找，或者用 `lv_obj_get_user_data` 单独存。

### 11.4 动画回调

动画回调签名是 `void (*exec_cb)(void *obj, int32_t v)`，不是 `lv_event_t *`，
无法直接用 `LvglMemberEventThunk`。最简单的改法是保留为静态成员函数：

```cpp
// 在 Dashboard 类里声明为 static
static void AnimTempExecCb(void *obj, int32_t v);
static void AnimHumiExecCb(void *obj, int32_t v);
static void AnimDistExecCb(void *obj, int32_t v);
static void AnimLuxExecCb(void *obj, int32_t v);
static void AlertYExecCb(void *obj, int32_t v);
static void AlertHideReadyCb(lv_anim_t *a);
```

`static` 成员函数有 C 兼容的函数指针，可以直接传给 `lv_anim_set_exec_cb`。
内部访问 Dashboard 成员通过 `Dashboard::instance()` 单例拿到。

### 11.5 机械替换清单

除了上面的回调改法，其余是机械替换：

| 原 C 写法 | 改后 C++ 写法 |
|---|---|
| `(lv_color_t){...}` 复合字面量 | `lv_color_t{...}` |
| `(chart_meta_t){ .src_chart = ... }` | `chart_meta_t{ nullptr, ... }` 位置初始化 |
| `(uint8_t)lv_slider_get_value(sl)` | `static_cast<uint8_t>(lv_slider_get_value(sl))` |
| `(lv_coord_t)x` | `static_cast<lv_coord_t>(x)` |
| `NULL` | `nullptr` |
| `g_xxx` 全局变量 | `xxx_` 成员变量 |
| `static embedmq_config_t cfg = { .field = val }` | `embedmq_config_t cfg{val1, val2, val3}` |

### 11.6 ui_handlers.cpp 对应修改

`ui_handlers.c` 里的 `dashboard_update_*` 调用改成 `Dashboard::instance().update_*(...)`:

```cpp
extern "C" void ui_on_dht11(const void *payload, size_t size, void *ctx)
{
    (void)ctx; (void)size;
    const auto *ev = static_cast<const evt_dht11_t *>(payload);
    Dashboard::instance().update_dht11(ev->temperature, ev->humidity);
}
```

其余 8 个 handler 同理。

### 11.7 改完后 C++ 占比估算

| 模块 | 行数 | 语言 |
|---|---|---|
| ui_dashboard.cpp + ui_handlers.cpp + ui_app.cpp | ~1,420 | C++ |
| algo/ + storage/ + daemon/ + sensors/ + input/ + network/ + sensor_daemon.cpp | ~1,309 | C++ |
| ipc_socket/mq/shm + ipc_protocol.h | ~385 | C（保持） |
| common/ + sim/ + fonts/ | ~31,900 | C（fonts 自动生成不算） |

**业务代码（去掉字体）C++ 占比：约 2,729 / 3,114 = 87%**
