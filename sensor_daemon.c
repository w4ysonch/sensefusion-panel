#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include "app/app_init.h"
#include "app/app_events.h"
#include "app/daemon_handlers.h"
#include "sensors/sensor_dht11.h"
#include "sensors/sensor_adxl345.h"
#include "sensors/sensor_sr501.h"
#include "sensors/sensor_sr04.h"
#include "sensors/sensor_light.h"
#include "algo/anomaly.h"
#include "storage/db.h"
#include "storage/settings.h"
#include "network/mqtt_client.h"
#include "ipc/ipc_socket.h"
#include "ipc/ipc_mq.h"
#include "ipc/ipc_shm.h"

#ifdef SIMULATOR
#  define DB_PATH   "./sensefusion.db"
#  define MQTT_HOST "localhost"
#else
#  define DB_PATH   "/var/lib/sensefusion/data.db"
#  define MQTT_HOST "mqtt.local"
#endif
#define MQTT_PORT 1883
#define MQTT_ID   "sensefusion-daemon"

/* 定义全局 embedmq 实例（sensor 文件通过 app_init.h extern 引用此变量） */
embedmq_t *g_mq = NULL;

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

    /* ── IPC 初始化 ──────────────────────────────────────────── */
    int server_fd = ipc_socket_server_init();
    if (server_fd < 0) {
        fprintf(stderr, "[daemon] UDS socket 初始化失败\n");
        return 1;
    }

    mqd_t alert_mq = ipc_mq_sender_open();
    if (alert_mq == (mqd_t)-1) {
        fprintf(stderr, "[daemon] POSIX mq 初始化失败\n");
        return 1;
    }

    if (ipc_shm_init(O_CREAT) != 0) {
        fprintf(stderr, "[daemon] 共享内存初始化失败\n");
        return 1;
    }

    /* ── 配置加载 ─────────────────────────────────────────────── */
    app_settings_t settings;
    settings_load(&settings);
    algo_anomaly_set_threshold(settings.anomaly_threshold);
    /* 将初始配置写入共享内存，供 ui_app 读取初始阈值 */
    ipc_shm_write_settings(&settings);

    /* ── 数据层与网络 ─────────────────────────────────────────── */
    if (db_init(DB_PATH) != 0)
        fprintf(stderr, "[daemon] 数据库初始化失败，将无 SQLite 日志\n");

    if (mqtt_init(MQTT_HOST, MQTT_PORT, MQTT_ID) != 0)
        fprintf(stderr, "[daemon] MQTT 初始化失败，将无网络上报\n");

    /* ── embedmq 初始化，注册 daemon 侧 handlers ──────────────── */
    static embedmq_config_t cfg = {
        .queue_size   = 4096,
        .max_msg_size = 64,
        .max_handlers = 16,
    };
    g_mq = embedmq_create(&cfg);
    if (!g_mq) {
        fprintf(stderr, "[daemon] embedmq_create 失败\n");
        return 1;
    }
    embedmq_register(g_mq, EVT_SENSOR_DHT11,   daemon_on_dht11,   NULL);
    embedmq_register(g_mq, EVT_SENSOR_ADXL345, daemon_on_adxl345, NULL);
    embedmq_register(g_mq, EVT_SENSOR_SR501,   daemon_on_sr501,   NULL);
    embedmq_register(g_mq, EVT_SENSOR_SR04,    daemon_on_sr04,    NULL);
    embedmq_register(g_mq, EVT_SENSOR_LIGHT,   daemon_on_light,   NULL);
    embedmq_register(g_mq, EVT_ALGO_COMFORT,   daemon_on_comfort, NULL);
    embedmq_register(g_mq, EVT_ALERT_ANOMALY,  daemon_on_anomaly, NULL);

    /* ── 启动 5 个传感器线程 ──────────────────────────────────── */
    pthread_t threads[5];
    pthread_create(&threads[0], NULL, sensor_dht11_thread,   NULL);
    pthread_create(&threads[1], NULL, sensor_adxl345_thread, NULL);
    pthread_create(&threads[2], NULL, sensor_sr501_thread,   NULL);
    pthread_create(&threads[3], NULL, sensor_sr04_thread,    NULL);
    pthread_create(&threads[4], NULL, sensor_light_thread,   NULL);

    /* ── 等待 ui_app 连接（阻塞）────────────────────────────── */
    printf("[daemon] 等待 ui_app 连接...\n");
    int client_fd = ipc_socket_server_accept(server_fd);
    if (client_fd < 0) {
        fprintf(stderr, "[daemon] accept 失败\n");
        g_running = 0;
    } else {
        daemon_handlers_set_ipc(client_fd, alert_mq);
    }

    /* ── 主循环：定期同步 shm 中的阈值设置 ───────────────────── */
    float last_threshold = settings.anomaly_threshold;
    while (g_running) {
        sleep(1);
        app_settings_t s;
        ipc_shm_read_settings(&s);
        if (s.magic == SETTINGS_MAGIC &&
            s.anomaly_threshold != last_threshold) {
            algo_anomaly_set_threshold(s.anomaly_threshold);
            last_threshold = s.anomaly_threshold;
            printf("[daemon] 异常阈值更新: %.2f g\n", last_threshold);
        }
    }

    /* ── 清理 ────────────────────────────────────────────────── */
    for (int i = 0; i < 5; i++)
        pthread_cancel(threads[i]);
    for (int i = 0; i < 5; i++)
        pthread_join(threads[i], NULL);

    if (client_fd >= 0) close(client_fd);
    close(server_fd);
    unlink(IPC_SOCKET_PATH);

    ipc_mq_close(alert_mq);
    ipc_mq_unlink();

    ipc_shm_close();
    ipc_shm_unlink();

    mqtt_deinit();
    db_cleanup_old(30);
    db_deinit();
    embedmq_destroy(g_mq);

    return 0;
}
