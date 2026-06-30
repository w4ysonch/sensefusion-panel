#include <stdio.h>
#include <signal.h>
#include "ui_ipc.h"

#include "../common/app_common.h"
#include "../ipc/ipc_socket.h"
#include "../ipc/ipc_mq.h"
#include "../ipc/ipc_protocol.h"

static int   s_sock_fd  = -1;
static mqd_t s_alert_mq = (mqd_t)-1;

void ui_ipc_set_fds(int sock_fd, mqd_t alert_mq)
{
    s_sock_fd  = sock_fd;
    s_alert_mq = alert_mq;
}

/* ── UDS 接收线程：ipc_frame_t → embedmq_post ───────────────────── */
void *ui_ipc_recv_thread(void *arg)
{
    (void)arg;
    ipc_frame_t f;

    uint32_t uuid_dht11   = embedmq_uuid(EVT_SENSOR_DHT11);
    uint32_t uuid_adxl345 = embedmq_uuid(EVT_SENSOR_ADXL345);
    uint32_t uuid_sr501   = embedmq_uuid(EVT_SENSOR_SR501);
    uint32_t uuid_sr04    = embedmq_uuid(EVT_SENSOR_SR04);
    uint32_t uuid_light   = embedmq_uuid(EVT_SENSOR_LIGHT);
    uint32_t uuid_comfort = embedmq_uuid(EVT_ALGO_COMFORT);

    while (ipc_socket_recv(s_sock_fd, &f) == 0) {
        switch ((ipc_msg_type_t)f.type) {
        case IPC_MSG_DHT11: {
            evt_dht11_t ev = { f.payload.dht11.temperature,
                               f.payload.dht11.humidity };
            embedmq_post_id(g_mq, uuid_dht11, &ev, sizeof(ev));
            break;
        }
        case IPC_MSG_ADXL345: {
            evt_adxl345_t ev = { f.payload.adxl345.x, f.payload.adxl345.y,
                                 f.payload.adxl345.z, f.payload.adxl345.magnitude };
            embedmq_post_id(g_mq, uuid_adxl345, &ev, sizeof(ev));
            break;
        }
        case IPC_MSG_SR501: {
            evt_sr501_t ev = { f.payload.sr501.detected };
            embedmq_post_id(g_mq, uuid_sr501, &ev, sizeof(ev));
            break;
        }
        case IPC_MSG_SR04: {
            evt_sr04_t ev = { f.payload.sr04.distance_cm };
            embedmq_post_id(g_mq, uuid_sr04, &ev, sizeof(ev));
            break;
        }
        case IPC_MSG_LIGHT: {
            evt_light_t ev = { f.payload.light.lux };
            embedmq_post_id(g_mq, uuid_light, &ev, sizeof(ev));
            break;
        }
        case IPC_MSG_COMFORT: {
            evt_comfort_t ev = { f.payload.comfort.heat_index,
                                 f.payload.comfort.level };
            embedmq_post_id(g_mq, uuid_comfort, &ev, sizeof(ev));
            break;
        }
        default:
            break;
        }
    }

    fprintf(stderr, "[ui_ipc] daemon 连接断开\n");
    raise(SIGTERM);
    return NULL;
}

/* ── POSIX mq 接收线程：ipc_alert_t → embedmq_post ─────────────── */
void *ui_ipc_alert_thread(void *arg)
{
    (void)arg;
    ipc_alert_t alert;
    uint32_t uuid_anomaly = embedmq_uuid(EVT_ALERT_ANOMALY);

    while (ipc_mq_recv_alert(s_alert_mq, &alert) == 0) {
        evt_anomaly_t ev = { alert.type, alert.magnitude };
        embedmq_post_id(g_mq, uuid_anomaly, &ev, sizeof(ev));
    }
    return NULL;
}
