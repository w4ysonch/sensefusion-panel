#ifndef IPC_PROTOCOL_H
#define IPC_PROTOCOL_H

#include <stdint.h>
#include "../storage/settings.h"

/* ── 资源名称 ──────────────────────────────────────────────────── */

/* Unix Domain Socket 路径（传感器数据流，daemon→ui） */
#define IPC_SOCKET_PATH      "/tmp/sensefusion.sock"

/* POSIX 消息队列名（异常告警，daemon→ui） */
#define IPC_MQ_ALERT_NAME    "/sensefusion_alert"

/* 共享内存名（配置同步，ui→daemon） */
#define IPC_SHM_SETTINGS_NAME  "/sensefusion_settings"

/* 共享内存保护信号量名 */
#define IPC_SEM_NAME           "/sensefusion_sem"

/* ── UDS 消息类型枚举 ──────────────────────────────────────────── */

typedef enum {
    IPC_MSG_DHT11   = 1,
    IPC_MSG_ADXL345 = 2,
    IPC_MSG_SR501   = 3,
    IPC_MSG_SR04    = 4,
    IPC_MSG_LIGHT   = 5,
    IPC_MSG_COMFORT = 6,
} ipc_msg_type_t;

/* ── UDS 各 payload 结构体 ─────────────────────────────────────── */

typedef struct { float temperature; float humidity; }          ipc_dht11_t;
typedef struct { float x; float y; float z; float magnitude; } ipc_adxl345_t;
typedef struct { uint8_t detected; }                           ipc_sr501_t;
typedef struct { float distance_cm; }                          ipc_sr04_t;
typedef struct { uint16_t lux; }                               ipc_light_t;
typedef struct { float heat_index; uint8_t level; }            ipc_comfort_t;

/* ── UDS 定长帧（每次 write/read 传输 sizeof(ipc_frame_t) 字节） ─ */

typedef struct {
    uint8_t type;       /* ipc_msg_type_t */
    uint8_t _pad[3];    /* 对齐 */
    union {
        ipc_dht11_t   dht11;
        ipc_adxl345_t adxl345;
        ipc_sr501_t   sr501;
        ipc_sr04_t    sr04;
        ipc_light_t   light;
        ipc_comfort_t comfort;
    } payload;
} ipc_frame_t;

/* ── POSIX mq 告警消息 ─────────────────────────────────────────── */

typedef struct {
    uint8_t type;       /* 异常类型：1=震动/冲击 */
    uint8_t _pad[3];
    float   magnitude;  /* 偏差幅度（g） */
} ipc_alert_t;

/* ── 共享内存布局 ───────────────────────────────────────────────── */
/* 共享内存直接映射为 app_settings_t（定义在 storage/settings.h）。
 * 信号量 IPC_SEM_NAME 保护并发读写。                              */

#endif /* IPC_PROTOCOL_H */
