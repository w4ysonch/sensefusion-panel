#include "daemon_handlers.h"
#include "../common/app_common.h"
#include "../common/g_running.hpp"
#include "../algo/comfort_index.hpp"
#include "../algo/anomaly.hpp"
#include "../storage/db.h"
#include "../network/mqtt_client.h"
#include "../ipc/ipc_socket.h"
#include "../ipc/ipc_mq.h"
#include <cstdio>
#include <cstring>
#include <unistd.h>

static int   s_sock_fd = -1;
static mqd_t s_mq      = static_cast<mqd_t>(-1);

void daemon_handlers_set_ipc(int socket_fd, mqd_t alert_mq)
{
    s_sock_fd = socket_fd;
    s_mq      = alert_mq;
}

/* UI 断开时调用：关闭 fd 并通知主循环退出 */
static void on_ui_disconnected()
{
    fprintf(stderr, "[daemon] UI 连接断开，准备退出\n");
    close(s_sock_fd);
    s_sock_fd = -1;
    g_running.store(false, std::memory_order_relaxed);
}

void daemon_on_dht11(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_dht11_t)) return;
    const auto *ev = static_cast<const evt_dht11_t *>(payload);

    if (s_sock_fd >= 0) {
        ipc_frame_t f{};
        f.type = IPC_MSG_DHT11;
        f.payload.dht11.temperature = ev->temperature;
        f.payload.dht11.humidity    = ev->humidity;
        if (ipc_socket_send(s_sock_fd, &f) < 0) { on_ui_disconnected(); return; }
    }

    algo_comfort_on_dht11(payload, size, nullptr);
    db_log_dht11(ev->temperature, ev->humidity);
    mqtt_publish_dht11(ev->temperature, ev->humidity);
}

void daemon_on_adxl345(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_adxl345_t)) return;
    const auto *ev = static_cast<const evt_adxl345_t *>(payload);

    if (s_sock_fd >= 0) {
        ipc_frame_t f{};
        f.type = IPC_MSG_ADXL345;
        f.payload.adxl345.x         = ev->x;
        f.payload.adxl345.y         = ev->y;
        f.payload.adxl345.z         = ev->z;
        f.payload.adxl345.magnitude = ev->magnitude;
        if (ipc_socket_send(s_sock_fd, &f) < 0) { on_ui_disconnected(); return; }
    }

    algo_anomaly_on_adxl345(payload, size, nullptr);
    db_log_adxl345(ev->x, ev->y, ev->z, ev->magnitude);
    mqtt_publish_adxl345(ev->x, ev->y, ev->z, ev->magnitude);
}

void daemon_on_sr501(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_sr501_t)) return;
    const auto *ev = static_cast<const evt_sr501_t *>(payload);

    if (s_sock_fd >= 0) {
        ipc_frame_t f{};
        f.type = IPC_MSG_SR501;
        f.payload.sr501.detected = ev->detected;
        if (ipc_socket_send(s_sock_fd, &f) < 0) { on_ui_disconnected(); return; }
    }

    db_log_sr501(ev->detected);
    mqtt_publish_sr501(ev->detected);
}

void daemon_on_sr04(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_sr04_t)) return;
    const auto *ev = static_cast<const evt_sr04_t *>(payload);

    if (s_sock_fd >= 0) {
        ipc_frame_t f{};
        f.type = IPC_MSG_SR04;
        f.payload.sr04.distance_cm = ev->distance_cm;
        if (ipc_socket_send(s_sock_fd, &f) < 0) { on_ui_disconnected(); return; }
    }

    db_log_sr04(ev->distance_cm);
    mqtt_publish_sr04(ev->distance_cm);
}

void daemon_on_light(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_light_t)) return;
    const auto *ev = static_cast<const evt_light_t *>(payload);

    if (s_sock_fd >= 0) {
        ipc_frame_t f{};
        f.type = IPC_MSG_LIGHT;
        f.payload.light.lux = ev->lux;
        if (ipc_socket_send(s_sock_fd, &f) < 0) { on_ui_disconnected(); return; }
    }

    db_log_light(ev->lux);
    mqtt_publish_light(ev->lux);
}

void daemon_on_comfort(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_comfort_t)) return;
    const auto *ev = static_cast<const evt_comfort_t *>(payload);

    if (s_sock_fd >= 0) {
        ipc_frame_t f{};
        f.type = IPC_MSG_COMFORT;
        f.payload.comfort.heat_index = ev->heat_index;
        f.payload.comfort.level      = ev->level;
        if (ipc_socket_send(s_sock_fd, &f) < 0) { on_ui_disconnected(); return; }
    }

    db_log_comfort(ev->heat_index, ev->level);
    mqtt_publish_comfort(ev->heat_index, ev->level);
}

void daemon_on_anomaly(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_anomaly_t)) return;
    const auto *ev = static_cast<const evt_anomaly_t *>(payload);

    if (s_mq != static_cast<mqd_t>(-1)) {
        ipc_alert_t a{};
        a.type      = ev->type;
        a.magnitude = ev->magnitude;
        ipc_mq_send_alert(s_mq, &a);
    }

    db_log_anomaly(ev->type, ev->magnitude);
    mqtt_publish_anomaly(ev->type, ev->magnitude);
}
