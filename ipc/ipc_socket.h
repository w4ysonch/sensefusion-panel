#ifndef IPC_SOCKET_H
#define IPC_SOCKET_H

#include "ipc_protocol.h"


#ifdef __cplusplus
extern "C" {
#endif

/* ── daemon 侧（UDS 服务端） ──────────────────────────────────── */

/* 创建、bind、listen，返回 server_fd；失败返回 -1 */
int  ipc_socket_server_init(void);

/* 阻塞等待 ui_app 连接，返回 client_fd；失败返回 -1 */
int  ipc_socket_server_accept(int server_fd);

/* 向 ui_app 发送一帧；返回 0=成功，-1=连接断开（对端已关闭） */
int  ipc_socket_send(int client_fd, const ipc_frame_t *frame);

/* ── ui_app 侧（UDS 客户端） ──────────────────────────────────── */

/* 连接 daemon，返回 fd；失败返回 -1 */
int  ipc_socket_client_connect(void);

/* 阻塞读一完整帧；返回 0=成功，-1=连接断开 */
int  ipc_socket_recv(int fd, ipc_frame_t *frame);


#ifdef __cplusplus
}
#endif

#endif /* IPC_SOCKET_H */
