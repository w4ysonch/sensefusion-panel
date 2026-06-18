#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include "app/app_init.h"
#include "sensors/sensor_dht11.h"
#include "sensors/sensor_adxl345.h"
#include "sensors/sensor_sr501.h"
#include "sensors/sensor_sr04.h"
#include "sensors/sensor_light.h"
#include "input/input_touch.h"
#include "input/input_ir.h"
#include "ui/ui_dashboard.h"
#include "lvgl/lvgl.h"

static volatile int g_running = 1;

static void on_sigint(int sig)
{
    (void)sig;
    g_running = 0;
}

int main(void)
{
    signal(SIGINT,  on_sigint);
    signal(SIGTERM, on_sigint);

    if (app_init() != 0) {
        fprintf(stderr, "app_init 失败\n");
        return 1;
    }

    /* dashboard_init 内部调用 lv_init() + HAL 初始化 + 创建界面 */
    dashboard_init();

    /* 启动所有传感器 / 输入线程 */
    pthread_t threads[7];
    pthread_create(&threads[0], NULL, sensor_dht11_thread,   NULL);
    pthread_create(&threads[1], NULL, sensor_adxl345_thread, NULL);
    pthread_create(&threads[2], NULL, sensor_sr501_thread,   NULL);
    pthread_create(&threads[3], NULL, sensor_sr04_thread,    NULL);
    pthread_create(&threads[4], NULL, sensor_light_thread,   NULL);
    pthread_create(&threads[5], NULL, input_touch_thread,    NULL);
    pthread_create(&threads[6], NULL, input_ir_thread,       NULL);

    /* 主循环：驱动 LVGL 渲染
     * lv_timer_handler() 返回下次需要处理的最短等待时间（ms） */
    while (g_running) {
        dashboard_tick();
    }

    for (int i = 0; i < 7; i++)
        pthread_cancel(threads[i]);
    for (int i = 0; i < 7; i++)
        pthread_join(threads[i], NULL);

    app_deinit();
    return 0;
}
