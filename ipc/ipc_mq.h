#ifndef IPC_MQ_H
#define IPC_MQ_H

#include <mqueue.h>
#include "ipc_protocol.h"

/* ── daemon 侧 ────────────────────────────────────────────────── */

/* 创建并打开消息队列（O_WRONLY|O_CREAT），返回 mqd_t；失败返回 (mqd_t)-1 */
mqd_t ipc_mq_sender_open(void);

/* 发送告警消息（非阻塞，队列满则丢弃并打印警告） */
void  ipc_mq_send_alert(mqd_t mq, const ipc_alert_t *alert);

/* ── ui_app 侧 ────────────────────────────────────────────────── */

/* 打开消息队列（O_RDONLY），返回 mqd_t；失败返回 (mqd_t)-1 */
mqd_t ipc_mq_receiver_open(void);

/* 阻塞接收告警消息；返回 0=成功，-1=出错 */
int   ipc_mq_recv_alert(mqd_t mq, ipc_alert_t *alert);

/* 关闭消息队列（两侧共用） */
void  ipc_mq_close(mqd_t mq);

/* daemon 退出时销毁队列（unlink） */
void  ipc_mq_unlink(void);

#endif /* IPC_MQ_H */
