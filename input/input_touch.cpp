#include <cstdio>
#include "input_touch.h"
#include "../common/app_common.h"

#include "../common/g_running.hpp"

#ifdef SIMULATOR

void *input_touch_thread(void *arg)
{
    (void)arg;
    return nullptr;
}

#else

#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

#define TOUCH_DEVICE "/dev/input/event1"

void *input_touch_thread(void *arg)
{
    (void)arg;

    int fd = open(TOUCH_DEVICE, O_RDONLY);
    if (fd < 0) {
        perror("打开触摸屏设备失败");
        return nullptr;
    }

    uint32_t uuid = embedmq_uuid(EVT_INPUT_TOUCH);
    struct input_event ie;
    int32_t x = 0, y = 0;
    uint8_t pressed = 0;

    while (g_running.load()) {
        if (read(fd, &ie, sizeof(ie)) < 0)
            break;

        if (ie.type == EV_KEY && ie.code == BTN_TOUCH) {
            pressed = static_cast<uint8_t>(ie.value);
        } else if (ie.type == EV_ABS) {
            if (ie.code == ABS_MT_POSITION_X) x = ie.value;
            if (ie.code == ABS_MT_POSITION_Y) y = ie.value;
        } else if (ie.type == EV_SYN && ie.code == SYN_REPORT) {
            evt_touch_t ev{};
            ev.x       = x;
            ev.y       = y;
            ev.pressed = pressed;
            embedmq_post_id(g_mq, uuid, &ev, sizeof(ev));
        }
    }

    close(fd);
    return nullptr;
}

#endif /* SIMULATOR */
