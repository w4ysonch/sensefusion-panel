#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include "common/app_common.h"
#include "ui/ui_handlers.h"
#include "ui/ui_dashboard.h"
#include "ui/ui_ipc.h"
#include "input/input_touch.h"
#include "input/input_ir.h"
#include "storage/settings.h"
#include "storage/db.h"
#include "ipc/ipc_socket.h"
#include "ipc/ipc_mq.h"
#include "ipc/ipc_shm.h"

#ifdef SIMULATOR
#  define DB_PATH  "./sensefusion.db"
#else
#  define DB_PATH  "/var/lib/sensefusion/data.db"
#endif

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

    /* ── embedmq 初始化，注册 ui 侧 handlers ─────────────────── */
    static embedmq_config_t cfg = {
        .queue_size   = 4096,
        .max_msg_size = 64,
        .max_handlers = 16,
    };
    g_mq = embedmq_create(&cfg);
    if (!g_mq) {
        fprintf(stderr, "[ui_app] embedmq_create 失败\n");
        return 1;
    }
    embedmq_register(g_mq, EVT_SENSOR_DHT11,   ui_on_dht11,   NULL);
    embedmq_register(g_mq, EVT_SENSOR_ADXL345, ui_on_adxl345, NULL);
    embedmq_register(g_mq, EVT_SENSOR_SR501,   ui_on_sr501,   NULL);
    embedmq_register(g_mq, EVT_SENSOR_SR04,    ui_on_sr04,    NULL);
    embedmq_register(g_mq, EVT_SENSOR_LIGHT,   ui_on_light,   NULL);
    embedmq_register(g_mq, EVT_ALGO_COMFORT,   ui_on_comfort, NULL);
    embedmq_register(g_mq, EVT_ALERT_ANOMALY,  ui_on_anomaly, NULL);
    embedmq_register(g_mq, EVT_INPUT_TOUCH,    ui_on_touch,   NULL);
    embedmq_register(g_mq, EVT_INPUT_IR,       ui_on_ir,      NULL);

    /* ── 加载配置 ─────────────────────────────────────────────── */
    app_settings_t settings;
    settings_load(&settings);

    /* ── 连接 daemon（重试直到成功）────────────────────────────── */
    printf("[ui_app] 等待 sensor_daemon 启动...\n");
    int sock_fd = -1;
    while (sock_fd < 0) {
        sock_fd = ipc_socket_client_connect();
        if (sock_fd < 0)
            sleep(1);
    }

    /* ── 打开 POSIX mq 和共享内存 ────────────────────────────── */
    mqd_t alert_mq = ipc_mq_receiver_open();
    if (alert_mq == (mqd_t)-1) {
        fprintf(stderr, "[ui_app] POSIX mq 打开失败\n");
        return 1;
    }

    if (ipc_shm_init(0) != 0) {
        fprintf(stderr, "[ui_app] 共享内存打开失败\n");
        return 1;
    }

    /* ── 注入 IPC fd，启动 IPC 接收线程 ─────────────────────── */
    ui_ipc_set_fds(sock_fd, alert_mq);

    pthread_t t_recv, t_alert, t_touch, t_ir;
    pthread_create(&t_recv,  NULL, ui_ipc_recv_thread,  NULL);
    pthread_create(&t_alert, NULL, ui_ipc_alert_thread, NULL);

    /* ── 打开数据库（只读查询 + 用户触发的清理操作）────────────── */
    if (db_init(DB_PATH) != 0)
        fprintf(stderr, "[ui_app] 数据库打开失败，系统页统计将不可用\n");

    /* ── 初始化 LVGL 界面 ─────────────────────────────────────── */
    dashboard_init(&settings);

    /* ── 启动输入线程（dashboard_init 之后确保 LVGL 已就绪）──── */
    pthread_create(&t_touch, NULL, input_touch_thread, NULL);
    pthread_create(&t_ir,    NULL, input_ir_thread,    NULL);

    /* ── 主循环 ──────────────────────────────────────────────── */
    while (g_running) {
        uint32_t wait_ms = dashboard_tick();
        if (wait_ms > 1)
            usleep((useconds_t)(wait_ms - 1) * 1000u);
    }

    /* ── 清理 ────────────────────────────────────────────────── */
    pthread_cancel(t_recv);
    pthread_cancel(t_alert);
    pthread_cancel(t_touch);
    pthread_cancel(t_ir);
    pthread_join(t_recv,  NULL);
    pthread_join(t_alert, NULL);
    pthread_join(t_touch, NULL);
    pthread_join(t_ir,    NULL);

    ipc_mq_close(alert_mq);
    ipc_shm_close();
    if (sock_fd >= 0) close(sock_fd);
    db_deinit();
    embedmq_destroy(g_mq);

    return 0;
}
