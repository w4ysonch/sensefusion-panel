#ifndef APP_INIT_H
#define APP_INIT_H

#include "embedmq.h"

/* 全局 embedmq 实例，所有模块共用 */
extern embedmq_t *g_mq;

int  app_init(void);
void app_deinit(void);

#endif /* APP_INIT_H */
