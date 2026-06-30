#include <stdio.h>
#include "input_touch.h"

#include "../common/app_common.h"

#ifdef SIMULATOR

/* 模拟器下触摸由 LVGL SDL 驱动直接处理（lv_sdl_mouse_create），
 * 此线程无需运行，立即退出即可。 */
void *input_touch_thread(void *arg)
{
    (void)arg;
    return NULL;
}

#else

#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

/* 触摸屏 input 设备路径，实际路径以 /proc/bus/input/devices 为准 */
#define TOUCH_DEVICE "/dev/input/event1"

void *input_touch_thread(void *arg)
{
    (void)arg;

    int fd = open(TOUCH_DEVICE, O_RDONLY);
    if (fd < 0) {
        perror("打开触摸屏设备失败");
        return NULL;
    }

    uint32_t uuid = embedmq_uuid(EVT_INPUT_TOUCH);
    struct input_event ie;
    int32_t x = 0, y = 0;
    uint8_t pressed = 0;

    while (1) {
        if (read(fd, &ie, sizeof(ie)) < 0)
            break;

        /* 多点触控 B 协议：累积坐标与按下状态，SYN_REPORT 时统一上报 */
        if (ie.type == EV_KEY && ie.code == BTN_TOUCH) {
            pressed = (uint8_t)ie.value;
        } else if (ie.type == EV_ABS) {
            if (ie.code == ABS_MT_POSITION_X) x = ie.value;
            if (ie.code == ABS_MT_POSITION_Y) y = ie.value;
        } else if (ie.type == EV_SYN && ie.code == SYN_REPORT) {
            evt_touch_t ev = { x, y, pressed };
            embedmq_post_id(g_mq, uuid, &ev, sizeof(ev));
        }
    }

    close(fd);
    return NULL;
}

#endif /* SIMULATOR */
