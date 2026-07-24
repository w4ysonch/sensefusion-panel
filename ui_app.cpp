#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <atomic>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>

#ifdef SIMULATOR
#  define DB_PATH   "./sensefusion.db"
#else
#  define DB_PATH   "/var/lib/sensefusion/data.db"
#endif

#include "common/app_common.h"
#include "common/g_running.hpp"
#include "ui/ui_dashboard.hpp"
#include "ui/ui_handlers.h"
#include "ui/ui_ipc.h"
#include "input/input_touch.h"
#include "input/input_ir.h"
#include "storage/settings.h"
#include "storage/db.h"
#include "ipc/ipc_socket.h"
#include "ipc/ipc_mq.h"
#include "ipc/ipc_shm.h"

std::atomic<bool> g_running{true};
embedmq_t        *g_mq = nullptr;

static void on_signal(int sig)
{
    (void)sig;
    g_running.store(false);
}

static int connect_with_retry(int retries, int delay_sec)
{
    for (int i = 0; i < retries; ++i) {
        int fd = ipc_socket_client_connect();
        if (fd >= 0) return fd;
        printf("[ui] 等待 daemon 启动 (%d/%d)...\n", i + 1, retries);
        sleep(delay_sec);
    }
    return -1;
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
        fprintf(stderr, "[ui] embedmq_create 失败\n");
        return 1;
    }

    /* 注册 ui 侧 handlers */
    embedmq_register(g_mq, EVT_SENSOR_DHT11,   ui_on_dht11,   nullptr);
    embedmq_register(g_mq, EVT_SENSOR_ADXL345, ui_on_adxl345, nullptr);
    embedmq_register(g_mq, EVT_SENSOR_SR501,   ui_on_sr501,   nullptr);
    embedmq_register(g_mq, EVT_SENSOR_SR04,    ui_on_sr04,    nullptr);
    embedmq_register(g_mq, EVT_SENSOR_LIGHT,   ui_on_light,   nullptr);
    embedmq_register(g_mq, EVT_ALGO_COMFORT,   ui_on_comfort, nullptr);
    embedmq_register(g_mq, EVT_ALERT_ANOMALY,  ui_on_anomaly, nullptr);
    embedmq_register(g_mq, EVT_INPUT_TOUCH,    ui_on_touch,   nullptr);
    embedmq_register(g_mq, EVT_INPUT_IR,       ui_on_ir,      nullptr);


    /* 连接 daemon UDS */
    int sock_fd = connect_with_retry(10, 1);
    if (sock_fd < 0) {
        fprintf(stderr, "[ui] 无法连接 daemon，退出\n");
        return 1;
    }

    /* POSIX mq 接收端 */
    mqd_t alert_mq = ipc_mq_receiver_open();

    /* 共享内存（附着方，daemon 已创建） */
    if (ipc_shm_init(0) != 0) {
        fprintf(stderr, "[ui] ipc_shm_init 失败\n");
    }

    /* 注入 IPC fd */
    ui_ipc_set_fds(sock_fd, alert_mq);

    /* 加载 settings（EEPROM/defaults） */
    app_settings_t settings{};
    settings_load(&settings);

    /* SQLite（ui 侧只读历史数据） */
    db_init(DB_PATH);

    /* 初始化 dashboard */
    Dashboard::instance().init(&settings);

    /* 启动 IPC 接收线程 */
    pthread_t recv_tid, alert_tid;
    pthread_create(&recv_tid,  nullptr, ui_ipc_recv_thread,  nullptr);
    if (alert_mq != static_cast<mqd_t>(-1))
        pthread_create(&alert_tid, nullptr, ui_ipc_alert_thread, nullptr);

    /* 启动 input 线程 */
    pthread_t touch_tid, ir_tid;
    pthread_create(&touch_tid, nullptr, input_touch_thread, nullptr);
    pthread_create(&ir_tid,    nullptr, input_ir_thread,    nullptr);

    printf("[ui] 启动完成，进入主循环\n");

    /* LVGL 主循环 */
    while (g_running.load()) {
        uint32_t delay_ms = Dashboard::instance().tick();
        if (delay_ms > 0)
            usleep(delay_ms * 1000u);
    }

    printf("[ui] 正在退出...\n");

    pthread_join(recv_tid,  nullptr);
    if (alert_mq != static_cast<mqd_t>(-1)) {
        ipc_mq_close(alert_mq);    /* unblocks mq_receive in alert thread */
        pthread_join(alert_tid, nullptr);
    }
    pthread_join(touch_tid, nullptr);
    pthread_join(ir_tid,    nullptr);

    ipc_shm_close();
    close(sock_fd);
    db_deinit();
    embedmq_destroy(g_mq);

    return 0;
}
