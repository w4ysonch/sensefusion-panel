#ifndef LV_DRV_SDL_H
#define LV_DRV_SDL_H

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 SDL2 显示和输入驱动，返回 display 对象
 * w/h 为窗口像素尺寸，与板子分辨率保持一致（800x480）*/
lv_display_t *sdl_hal_init(int32_t w, int32_t h);

#ifdef __cplusplus
}
#endif

#endif /* LV_DRV_SDL_H */
