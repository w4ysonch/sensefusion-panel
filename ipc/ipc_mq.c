#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include "ipc_mq.h"

/* 队列属性：最多 8 条告警，每条 sizeof(ipc_alert_t) 字节 */
static struct mq_attr s_attr = {
    .mq_flags   = 0,
    .mq_maxmsg  = 8,
    .mq_msgsize = sizeof(ipc_alert_t),
    .mq_curmsgs = 0,
};

mqd_t ipc_mq_sender_open(void)
{
    mqd_t mq = mq_open(IPC_MQ_ALERT_NAME,
                        O_WRONLY | O_CREAT | O_NONBLOCK,
                        0666, &s_attr);
    if (mq == (mqd_t)-1)
        perror("[ipc_mq] sender mq_open");
    else
        printf("[ipc_mq] 发送端已打开 %s\n", IPC_MQ_ALERT_NAME);
    return mq;
}

void ipc_mq_send_alert(mqd_t mq, const ipc_alert_t *alert)
{
    /* 优先级 1，高于普通消息 0 */
    if (mq_send(mq, (const char *)alert, sizeof(ipc_alert_t), 1) < 0) {
        if (errno == EAGAIN)
            fprintf(stderr, "[ipc_mq] 队列已满，告警丢弃\n");
        else
            perror("[ipc_mq] mq_send");
    }
}

mqd_t ipc_mq_receiver_open(void)
{
    /* 接收端不创建队列，等待 daemon 先创建 */
    mqd_t mq = mq_open(IPC_MQ_ALERT_NAME, O_RDONLY);
    if (mq == (mqd_t)-1)
        perror("[ipc_mq] receiver mq_open");
    else
        printf("[ipc_mq] 接收端已打开 %s\n", IPC_MQ_ALERT_NAME);
    return mq;
}

int ipc_mq_recv_alert(mqd_t mq, ipc_alert_t *alert)
{
    ssize_t n = mq_receive(mq, (char *)alert, sizeof(ipc_alert_t), NULL);
    if (n < 0) {
        if (errno != EINTR)
            perror("[ipc_mq] mq_receive");
        return -1;
    }
    return 0;
}

void ipc_mq_close(mqd_t mq)
{
    if (mq != (mqd_t)-1)
        mq_close(mq);
}

void ipc_mq_unlink(void)
{
    mq_unlink(IPC_MQ_ALERT_NAME);
}
