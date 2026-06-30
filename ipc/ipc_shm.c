#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <errno.h>
#include "ipc_shm.h"

static app_settings_t *s_shm = NULL;  /* mmap 映射指针 */
static int              s_fd  = -1;   /* shm_open 返回的 fd */
static sem_t           *s_sem = NULL; /* 命名信号量 */

int ipc_shm_init(int oflag)
{
    /* 打开或创建共享内存对象 */
    s_fd = shm_open(IPC_SHM_SETTINGS_NAME,
                    oflag | O_RDWR,
                    0666);
    if (s_fd < 0) {
        perror("[ipc_shm] shm_open");
        return -1;
    }

    /* 仅创建方负责设置大小 */
    if (oflag & O_CREAT) {
        if (ftruncate(s_fd, sizeof(app_settings_t)) < 0) {
            perror("[ipc_shm] ftruncate");
            close(s_fd);
            s_fd = -1;
            return -1;
        }
    }

    s_shm = (app_settings_t *)mmap(NULL, sizeof(app_settings_t),
                                   PROT_READ | PROT_WRITE,
                                   MAP_SHARED, s_fd, 0);
    if (s_shm == MAP_FAILED) {
        perror("[ipc_shm] mmap");
        close(s_fd);
        s_fd  = -1;
        s_shm = NULL;
        return -1;
    }

    /* 打开或创建信号量（初值 1，互斥用） */
    s_sem = sem_open(IPC_SEM_NAME,
                     (oflag & O_CREAT) ? O_CREAT : 0,
                     0666, 1);
    if (s_sem == SEM_FAILED) {
        perror("[ipc_shm] sem_open");
        munmap(s_shm, sizeof(app_settings_t));
        close(s_fd);
        s_shm = NULL;
        s_fd  = -1;
        return -1;
    }

    printf("[ipc_shm] 共享内存初始化完成 %s\n", IPC_SHM_SETTINGS_NAME);
    return 0;
}

void ipc_shm_close(void)
{
    if (s_sem) { sem_close(s_sem); s_sem = NULL; }
    if (s_shm) { munmap(s_shm, sizeof(app_settings_t)); s_shm = NULL; }
    if (s_fd >= 0) { close(s_fd); s_fd = -1; }
}

void ipc_shm_unlink(void)
{
    shm_unlink(IPC_SHM_SETTINGS_NAME);
    sem_unlink(IPC_SEM_NAME);
}

void ipc_shm_write_settings(const app_settings_t *s)
{
    if (!s_shm || !s_sem) return;
    sem_wait(s_sem);
    memcpy(s_shm, s, sizeof(app_settings_t));
    sem_post(s_sem);
}

void ipc_shm_read_settings(app_settings_t *s)
{
    if (!s_shm || !s_sem) return;
    sem_wait(s_sem);
    memcpy(s, s_shm, sizeof(app_settings_t));
    sem_post(s_sem);
}
