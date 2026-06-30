#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "ipc_socket.h"

int ipc_socket_server_init(void)
{
    /* 清理上次遗留的 socket 文件 */
    unlink(IPC_SOCKET_PATH);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("[ipc_socket] socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, IPC_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[ipc_socket] bind");
        close(fd);
        return -1;
    }

    if (listen(fd, 1) < 0) {
        perror("[ipc_socket] listen");
        close(fd);
        return -1;
    }

    printf("[ipc_socket] 服务端监听 %s\n", IPC_SOCKET_PATH);
    return fd;
}

int ipc_socket_server_accept(int server_fd)
{
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("[ipc_socket] accept");
        return -1;
    }
    printf("[ipc_socket] ui_app 已连接\n");
    return client_fd;
}

void ipc_socket_send(int client_fd, const ipc_frame_t *frame)
{
    const char *buf = (const char *)frame;
    size_t total = sizeof(ipc_frame_t);
    size_t sent  = 0;

    while (sent < total) {
        ssize_t n = write(client_fd, buf + sent, total - sent);
        if (n <= 0) {
            if (n < 0) perror("[ipc_socket] write");
            return;
        }
        sent += (size_t)n;
    }
}

int ipc_socket_client_connect(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("[ipc_socket] socket");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, IPC_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    printf("[ipc_socket] 已连接 daemon\n");
    return fd;
}

int ipc_socket_recv(int fd, ipc_frame_t *frame)
{
    char  *buf  = (char *)frame;
    size_t total = sizeof(ipc_frame_t);
    size_t rcvd  = 0;

    while (rcvd < total) {
        ssize_t n = read(fd, buf + rcvd, total - rcvd);
        if (n <= 0) {
            if (n < 0 && errno != EINTR)
                perror("[ipc_socket] read");
            return -1;
        }
        rcvd += (size_t)n;
    }
    return 0;
}
