#ifndef DAEMON_HANDLERS_H
#define DAEMON_HANDLERS_H

#include <stddef.h>
#include <mqueue.h>

/* sensor_daemon 在 ui_app 连接后调用，设置 IPC 发送通道 */
void daemon_handlers_set_ipc(int socket_fd, mqd_t alert_mq);

/* embedmq 回调，注册到 sensor_daemon 的 g_mq */
void daemon_on_dht11   (const void *payload, size_t size, void *ctx);
void daemon_on_adxl345 (const void *payload, size_t size, void *ctx);
void daemon_on_sr501   (const void *payload, size_t size, void *ctx);
void daemon_on_sr04    (const void *payload, size_t size, void *ctx);
void daemon_on_light   (const void *payload, size_t size, void *ctx);
void daemon_on_comfort (const void *payload, size_t size, void *ctx);
void daemon_on_anomaly (const void *payload, size_t size, void *ctx);

#endif /* DAEMON_HANDLERS_H */
