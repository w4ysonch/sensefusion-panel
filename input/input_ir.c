#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>
#include "input_ir.h"
#include "../app/app_init.h"
#include "../app/app_events.h"

/* 红外遥控 input 设备路径，具体编号以板子实际为准 */
#define IR_DEVICE "/dev/input/event1"

void *input_ir_thread(void *arg)
{
    (void)arg;
    uint32_t uuid = embedmq_uuid(EVT_INPUT_IR);

    int fd = open(IR_DEVICE, O_RDONLY);
    if (fd < 0) {
        perror("打开红外遥控设备失败");
        return NULL;
    }

    struct input_event ie;

    while (1) {
        if (read(fd, &ie, sizeof(ie)) < 0)
            break;

        /* 只在按键按下（value==1）时上报，不处理长按（value==2）和抬起（value==0） */
        if (ie.type == EV_KEY && ie.value == 1) {
            evt_ir_t ev;
            ev.key_code = ie.code;
            embedmq_post_id(g_mq, uuid, &ev, sizeof(ev));
        }
    }

    close(fd);
    return NULL;
}
