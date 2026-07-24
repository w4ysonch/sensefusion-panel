#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <atomic>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>

#ifdef SIMULATOR
#  define DB_PATH   "./sensefusion.db"
#  define MQTT_HOST "localhost"
#else
#  define DB_PATH   "/var/lib/sensefusion/data.db"
#  define MQTT_HOST "mqtt.local"
#endif
#define MQTT_PORT 1883
#define MQTT_ID   "sensefusion-daemon"

#include "common/app_common.h"
#include "common/g_running.hpp"
#include "daemon/daemon_handlers.h"
#include "sensors/sensor_dht11.h"
#include "sensors/sensor_adxl345.h"
#include "sensors/sensor_sr501.h"
#include "sensors/sensor_sr04.h"
#include "sensors/sensor_light.h"
#include "storage/db.h"
#include "storage/settings.h"
#include "ipc/ipc_socket.h"
#include "ipc/ipc_mq.h"
#include "ipc/ipc_shm.h"
#include "algo/anomaly.hpp"
#include "network/mqtt_client.h"

std::atomic<bool> g_running{true};
embedmq_t        *g_mq = nullptr;

static void on_signal(int sig)
{
    (void)sig;
    g_running.store(false);
}

static void *shm_sync_thread(void *arg)
{
    (void)arg;
    app_settings_t s{};
    float last_threshold = SETTINGS_DEFAULT_THRESHOLD;
    while (g_running.load()) {
        ipc_shm_read_settings(&s);
        if (s.magic == SETTINGS_MAGIC && s.anomaly_threshold != last_threshold) {
            algo_anomaly_set_threshold(s.anomaly_threshold);
            last_threshold = s.anomaly_threshold;
            printf("[daemon] 异常阈值更新: %.2f g\n", last_threshold);
        }
        sleep(1);
    }
    return nullptr;
}

int main(void)
{
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    /* embedmq */
    static embedmq_config_t cfg = {
        .queue_size      = 4096,
        .max_msg_size    = 1024,
        .max_handlers    = 16,
        .thread_priority = 0,
    };
    g_mq = embedmq_create(&cfg);
    if (!g_mq) {
        fprintf(stderr, "[daemon] embedmq_create 失败\n");
        return 1;
    }

    /* 注册 daemon 侧 handlers */
    embedmq_register(g_mq, EVT_SENSOR_DHT11,   daemon_on_dht11,   nullptr);
    embedmq_register(g_mq, EVT_SENSOR_ADXL345, daemon_on_adxl345, nullptr);
    embedmq_register(g_mq, EVT_SENSOR_SR501,   daemon_on_sr501,   nullptr);
    embedmq_register(g_mq, EVT_SENSOR_SR04,    daemon_on_sr04,    nullptr);
    embedmq_register(g_mq, EVT_SENSOR_LIGHT,   daemon_on_light,   nullptr);
    embedmq_register(g_mq, EVT_ALGO_COMFORT,   daemon_on_comfort, nullptr);
    embedmq_register(g_mq, EVT_ALERT_ANOMALY,  daemon_on_anomaly, nullptr);

    /* SQLite */
    if (db_init(DB_PATH) != 0) {
        fprintf(stderr, "[daemon] db_init 失败\n");
        return 1;
    }

    /* MQTT（可选，失败不阻塞） */
    if (mqtt_init(MQTT_HOST, MQTT_PORT, MQTT_ID) != 0)
        fprintf(stderr, "[daemon] mqtt_init 失败，继续运行（无 MQTT）\n");

    /* IPC: UDS server */
    int server_fd = ipc_socket_server_init();
    if (server_fd < 0) {
        fprintf(stderr, "[daemon] ipc_socket_server_init 失败\n");
        return 1;
    }

    /* IPC: POSIX mq sender */
    mqd_t alert_mq = ipc_mq_sender_open();
    if (alert_mq == static_cast<mqd_t>(-1)) {
        fprintf(stderr, "[daemon] ipc_mq_sender_open 失败\n");
    }

    /* IPC: shared memory (创建方) */
    if (ipc_shm_init(O_CREAT) != 0) {
        fprintf(stderr, "[daemon] ipc_shm_init 失败\n");
    } else {
        /* 写入默认 settings */
        app_settings_t def{};
        settings_load(&def);
        ipc_shm_write_settings(&def);
    }

    /* 启动 shm 同步线程 */
    pthread_t shm_tid;
    pthread_create(&shm_tid, nullptr, shm_sync_thread, nullptr);

    /* 等待 ui_app 连接（阻塞） */
    printf("[daemon] 等待 ui_app 连接...\n");
    int client_fd = ipc_socket_server_accept(server_fd);
    if (client_fd < 0) {
        fprintf(stderr, "[daemon] accept 失败，继续运行（无 UI）\n");
    }
    daemon_handlers_set_ipc(client_fd, alert_mq);

    /* 启动传感器线程 */
    pthread_t tids[5];
    pthread_create(&tids[0], nullptr, sensor_dht11_thread,   nullptr);
    pthread_create(&tids[1], nullptr, sensor_adxl345_thread, nullptr);
    pthread_create(&tids[2], nullptr, sensor_sr501_thread,   nullptr);
    pthread_create(&tids[3], nullptr, sensor_sr04_thread,    nullptr);
    pthread_create(&tids[4], nullptr, sensor_light_thread,   nullptr);

    printf("[daemon] 启动完成，运行中...\n");

    /* 主循环：仅等待退出信号 */
    while (g_running.load())
        sleep(1);

    printf("[daemon] 正在退出...\n");

    for (auto &tid : tids)
        pthread_join(tid, nullptr);
    pthread_join(shm_tid, nullptr);

    db_deinit();
    mqtt_deinit();
    ipc_shm_close();
    ipc_shm_unlink();
    ipc_mq_close(alert_mq);
    ipc_mq_unlink();
    if (client_fd >= 0) close(client_fd);
    close(server_fd);
    embedmq_destroy(g_mq);

    return 0;
}
