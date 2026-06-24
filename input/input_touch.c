#include <stdio.h>
#include "input_touch.h"
#include "../app/app_init.h"
#include "../app/app_events.h"

#ifdef SIMULATOR

/* 模拟器下触摸由 LVGL SDL 驱动直接处理（lv_sdl_mouse_create），
 * 此线程无需运行，立即退出即可。 */
void *input_touch_thread(void *arg)
{
    (void)arg;
    return NULL;
}

#else

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

/* 触摸屏 input 设备路径，实际路径以 /proc/bus/input/devices 为准 */
#define TOUCH_DEVICE "/dev/input/event0"

void *input_touch_thread(void *arg)
{
    (void)arg;
    uint32_t uuid = embedmq_uuid(EVT_INPUT_TOUCH);

    int fd = open(TOUCH_DEVICE, O_RDONLY);
    if (fd < 0) {
        perror("打开触摸屏设备失败");
        return NULL;
    }

    struct input_event ie;
    evt_touch_t ev = {0};

    while (1) {
        if (read(fd, &ie, sizeof(ie)) < 0)
            break;

        /* 多点触控 B 协议：累积坐标，SYN_REPORT 时统一上报 */
        if (ie.type == EV_ABS) {
            if (ie.code == ABS_MT_POSITION_X)  ev.x = ie.value;
            if (ie.code == ABS_MT_POSITION_Y)  ev.y = ie.value;
        } else if (ie.type == EV_SYN && ie.code == SYN_REPORT) {
            embedmq_post_id(g_mq, uuid, &ev, sizeof(ev));
        }
    }

    close(fd);
    return NULL;
}

#endif /* SIMULATOR */
