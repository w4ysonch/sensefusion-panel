#include <stdio.h>
#include "input_ir.h"

#include "../common/app_common.h"

#ifdef SIMULATOR

/* 模拟器下键盘输入由 LVGL SDL 驱动直接处理（lv_sdl_keyboard_create），
 * 此线程无需运行，立即退出即可。 */
void *input_ir_thread(void *arg)
{
    (void)arg;
    return NULL;
}

#else

#include <unistd.h>
#include <fcntl.h>
#include <linux/input.h>

/* 红外遥控 input 设备路径，具体编号以板子实际为准 */
#define IR_DEVICE "/dev/input/event_ir_todo"

void *input_ir_thread(void *arg)
{
    (void)arg;

    int fd = open(IR_DEVICE, O_RDONLY);
    if (fd < 0) {
        perror("打开红外遥控设备失败");
        return NULL;
    }

    uint32_t uuid = embedmq_uuid(EVT_INPUT_IR);
    struct input_event ie;

    while (1) {
        if (read(fd, &ie, sizeof(ie)) < 0)
            break;

        /* 只在按键按下（value==1）时上报，不处理长按（value==2）和抬起（value==0） */
        if (ie.type == EV_KEY && ie.value == 1) {
            evt_ir_t ev = { (uint16_t)ie.code };
            embedmq_post_id(g_mq, uuid, &ev, sizeof(ev));
        }
    }

    close(fd);
    return NULL;
}

#endif /* SIMULATOR */
