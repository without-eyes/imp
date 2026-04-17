#include "utils/ipc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

int ipc_send_message(const char* source_module, const char* level, const char* message) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, IMP_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(sock);
        return -1;
    }

    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "{\"source\":\"%s\", \"level\":\"%s\", \"message\":\"%s\"}",
             source_module, level, message);

    send(sock, buffer, strlen(buffer), 0);
    close(sock);
    
    return 0;
}