#include <stdio.h>
#include "app_init.h"
#include "app_events.h"
#include "../ui/ui_handlers.h"

embedmq_t *g_mq = NULL;

static embedmq_config_t s_cfg = {
    .queue_size   = 4096,  /* 环形缓冲区大小（字节） */
    .max_msg_size = 64,    /* 单条消息最大长度 */
    .max_handlers = 16,    /* 最多注册的事件处理器数量 */
};

int app_init(void)
{
    g_mq = embedmq_create(&s_cfg);
    if (!g_mq) {
        fprintf(stderr, "[app] embedmq_create 失败\n");
        return -1;
    }

    /* 注册所有事件处理器 */
    embedmq_register(g_mq, EVT_SENSOR_DHT11,   ui_on_dht11,   NULL);
    embedmq_register(g_mq, EVT_SENSOR_ADXL345, ui_on_adxl345, NULL);
    embedmq_register(g_mq, EVT_SENSOR_SR501,   ui_on_sr501,   NULL);
    embedmq_register(g_mq, EVT_SENSOR_SR04,    ui_on_sr04,    NULL);
    embedmq_register(g_mq, EVT_SENSOR_LIGHT,   ui_on_light,   NULL);
    embedmq_register(g_mq, EVT_ALGO_COMFORT,   ui_on_comfort, NULL);
    embedmq_register(g_mq, EVT_ALERT_ANOMALY,  ui_on_anomaly, NULL);
    embedmq_register(g_mq, EVT_INPUT_TOUCH,    ui_on_touch,   NULL);
    embedmq_register(g_mq, EVT_INPUT_IR,       ui_on_ir,      NULL);

    printf("[app] embedmq 初始化完成，已注册 9 个处理器\n");
    return 0;
}

void app_deinit(void)
{
    if (g_mq) {
        embedmq_destroy(g_mq);
        g_mq = NULL;
    }
}
