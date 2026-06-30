#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include "ui/ui_dashboard.h"
#include "input/input_touch.h"
#include "input/input_ir.h"
#include "storage/settings.h"
#include "ipc/ipc_socket.h"
#include "ipc/ipc_mq.h"
#include "ipc/ipc_shm.h"
#include "ipc/ipc_protocol.h"

static volatile int g_running = 1;
static int          g_sock_fd = -1;
static mqd_t        g_alert_mq = (mqd_t)-1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ── IPC 接收线程：UDS 传感器数据帧 → dashboard_update_* ──────── */
static void *ipc_recv_thread(void *arg)
{
    (void)arg;
    ipc_frame_t f;

    while (ipc_socket_recv(g_sock_fd, &f) == 0) {
        switch ((ipc_msg_type_t)f.type) {
        case IPC_MSG_DHT11:
            dashboard_update_dht11(f.payload.dht11.temperature,
                                   f.payload.dht11.humidity);
            break;
        case IPC_MSG_ADXL345:
            dashboard_update_accel(f.payload.adxl345.x,
                                   f.payload.adxl345.y,
                                   f.payload.adxl345.z,
                                   f.payload.adxl345.magnitude);
            break;
        case IPC_MSG_SR501:
            dashboard_update_pir(f.payload.sr501.detected);
            break;
        case IPC_MSG_SR04:
            dashboard_update_distance(f.payload.sr04.distance_cm);
            break;
        case IPC_MSG_LIGHT:
            dashboard_update_light(f.payload.light.lux);
            break;
        case IPC_MSG_COMFORT:
            dashboard_update_comfort(f.payload.comfort.heat_index,
                                     (comfort_level_t)f.payload.comfort.level);
            break;
        default:
            break;
        }
    }

    fprintf(stderr, "[ui_app] daemon 连接断开，停止接收\n");
    g_running = 0;
    return NULL;
}

/* ── 告警线程：POSIX mq 异常告警 → dashboard_show_alert ──────── */
static void *ipc_alert_thread(void *arg)
{
    (void)arg;
    ipc_alert_t alert;

    while (ipc_mq_recv_alert(g_alert_mq, &alert) == 0)
        dashboard_show_alert(alert.magnitude);

    return NULL;
}

int main(void)
{
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    /* ── 加载配置（EEPROM；模拟器下读取失败用默认值） ──────────── */
    app_settings_t settings;
    settings_load(&settings);

    /* ── 连接 daemon（重试直到成功）────────────────────────────── */
    printf("[ui_app] 等待 sensor_daemon 启动...\n");
    while (g_sock_fd < 0) {
        g_sock_fd = ipc_socket_client_connect();
        if (g_sock_fd < 0)
            sleep(1);
    }

    /* ── 打开 POSIX mq（daemon 已提前创建）──────────────────────── */
    g_alert_mq = ipc_mq_receiver_open();
    if (g_alert_mq == (mqd_t)-1) {
        fprintf(stderr, "[ui_app] POSIX mq 打开失败\n");
        return 1;
    }

    /* ── 打开共享内存（daemon 已提前创建）──────────────────────── */
    if (ipc_shm_init(0) != 0) {
        fprintf(stderr, "[ui_app] 共享内存打开失败\n");
        return 1;
    }

    /* ── 启动 IPC 接收线程 ────────────────────────────────────── */
    pthread_t t_recv, t_alert, t_touch, t_ir;
    pthread_create(&t_recv,  NULL, ipc_recv_thread,  NULL);
    pthread_create(&t_alert, NULL, ipc_alert_thread, NULL);

    /* ── 初始化 LVGL 界面 ─────────────────────────────────────── */
    dashboard_init(&settings);

    /* ── 启动输入线程（在 dashboard_init 之后，确保 LVGL 已就绪） */
    pthread_create(&t_touch, NULL, input_touch_thread, NULL);
    pthread_create(&t_ir,    NULL, input_ir_thread,    NULL);

    /* ── 主循环：dashboard_tick() 返回最短等待时间 ───────────── */
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

    ipc_mq_close(g_alert_mq);
    ipc_shm_close();
    if (g_sock_fd >= 0) close(g_sock_fd);

    return 0;
}
