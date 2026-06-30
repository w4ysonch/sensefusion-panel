#ifndef IPC_SHM_H
#define IPC_SHM_H

#include "ipc_protocol.h"
#include "../storage/settings.h"

/* 创建或打开共享内存，建立信号量（两侧都调用）
 * oflag: O_CREAT（daemon 侧首次创建）或 0（ui_app 侧打开已有）
 * 返回 0=成功，-1=失败 */
int  ipc_shm_init(int oflag);

/* 释放 mmap、关闭 fd、关闭信号量（两侧都调用，不 unlink） */
void ipc_shm_close(void);

/* daemon 退出时清理共享内存和信号量（unlink） */
void ipc_shm_unlink(void);

/* ui_app 侧：将 settings 写入共享内存（sem 保护） */
void ipc_shm_write_settings(const app_settings_t *s);

/* daemon 侧：从共享内存读取 settings（sem 保护） */
void ipc_shm_read_settings(app_settings_t *s);

#endif /* IPC_SHM_H */
