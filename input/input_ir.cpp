#include <cstdio>
#include "input_ir.h"
#include "../common/app_common.h"

#include "../common/g_running.hpp"

#ifdef SIMULATOR

void *input_ir_thread(void *arg)
{
    (void)arg;
    return nullptr;
}

#else

#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>

#define IR_DEVICE "/dev/input/event_ir_todo"

void *input_ir_thread(void *arg)
{
    (void)arg;

    int fd = open(IR_DEVICE, O_RDONLY);
    if (fd < 0) {
        perror("打开红外遥控设备失败");
        return nullptr;
    }

    uint32_t uuid = embedmq_uuid(EVT_INPUT_IR);
    struct input_event ie;

    while (g_running.load()) {
        if (read(fd, &ie, sizeof(ie)) < 0)
            break;

        if (ie.type == EV_KEY && ie.value == 1) {
            evt_ir_t ev{};
            ev.key_code = static_cast<uint16_t>(ie.code);
            embedmq_post_id(g_mq, uuid, &ev, sizeof(ev));
        }
    }

    close(fd);
    return nullptr;
}

#endif /* SIMULATOR */
