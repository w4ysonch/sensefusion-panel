#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include "app/app_init.h"
#include "sensors/sensor_dht11.h"
#include "sensors/sensor_adxl345.h"
#include "sensors/sensor_sr501.h"
#include "sensors/sensor_sr04.h"
#include "sensors/sensor_light.h"
#include "input/input_touch.h"
#include "input/input_ir.h"
#include "ui/ui_dashboard.h"
#include "storage/db.h"
#include "storage/settings.h"
#include "network/mqtt_client.h"
#include "algo/anomaly.h"
#include "lvgl/lvgl.h"

#ifdef SIMULATOR
#  define DB_PATH   "./sensefusion.db"
#  define MQTT_HOST "localhost"
#else
#  define DB_PATH   "/var/lib/sensefusion/data.db"
#  define MQTT_HOST "mqtt.local"
#endif
#define MQTT_PORT 1883
#define MQTT_ID   "sensefusion-panel"

static volatile int g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

int main(void)
{
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    /* embedmq 初始化（handler 注册在内部） */
    if (app_init() != 0) {
        fprintf(stderr, "app_init 失败\n");
        return 1;
    }

    /* 加载持久化配置（EEPROM，板子上生效；模拟器下读取失败用默认值） */
    app_settings_t settings;
    settings_load(&settings);
    algo_anomaly_set_threshold(settings.anomaly_threshold);

    /* SQLite 数据库 */
    if (db_init(DB_PATH) != 0)
        fprintf(stderr, "[main] 数据库初始化失败，将无 SQLite 日志\n");

    /* MQTT（连接失败不影响主功能） */
    if (mqtt_init(MQTT_HOST, MQTT_PORT, MQTT_ID) != 0)
        fprintf(stderr, "[main] MQTT 初始化失败，将无网络上报\n");

    /* LVGL + 界面初始化 */
    dashboard_init(&settings);

    /* 启动 7 个传感器/输入线程 */
    pthread_t threads[7];
    pthread_create(&threads[0], NULL, sensor_dht11_thread,   NULL);
    pthread_create(&threads[1], NULL, sensor_adxl345_thread, NULL);
    pthread_create(&threads[2], NULL, sensor_sr501_thread,   NULL);
    pthread_create(&threads[3], NULL, sensor_sr04_thread,    NULL);
    pthread_create(&threads[4], NULL, sensor_light_thread,   NULL);
    pthread_create(&threads[5], NULL, input_touch_thread,    NULL);
    pthread_create(&threads[6], NULL, input_ir_thread,       NULL);

    /* 主循环：dashboard_tick() 返回最短等待时间(ms)，减 1ms 作提前量 */
    while (g_running) {
        uint32_t wait_ms = dashboard_tick();
        if (wait_ms > 1)
            usleep((useconds_t)(wait_ms - 1) * 1000u);
    }

    for (int i = 0; i < 7; i++)
        pthread_cancel(threads[i]);
    for (int i = 0; i < 7; i++)
        pthread_join(threads[i], NULL);

    mqtt_deinit();
    db_cleanup_old(30);   /* 清理 30 天前的历史记录 */
    db_deinit();
    app_deinit();
    return 0;
}
