#ifndef UI_IPC_H
#define UI_IPC_H

#include <mqueue.h>

/* 注入 IPC 文件描述符，须在启动接收线程前调用 */
void ui_ipc_set_fds(int sock_fd, mqd_t alert_mq);

/* 接收线程：UDS → embedmq_post（传感器数据帧） */
void *ui_ipc_recv_thread(void *arg);

/* 接收线程：POSIX mq → embedmq_post（异常告警） */
void *ui_ipc_alert_thread(void *arg);

#endif /* UI_IPC_H */
