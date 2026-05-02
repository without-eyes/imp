#include "utils/ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "utils/cJSON.h"

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

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        close(sock);
        return -1;
    }

    cJSON_AddStringToObject(root, "source", source_module);
    cJSON_AddStringToObject(root, "level", level);
    cJSON_AddStringToObject(root, "message", message);

    char *json_string = cJSON_PrintUnformatted(root);
    
    if (json_string != NULL) {
        size_t len = strlen(json_string);
        ssize_t sent = send(sock, json_string, len, 0);
        
        free(json_string);
        
        if (sent < 0) {
            close(sock);
            return -1;
        }
    }

    cJSON_Delete(root);
    close(sock);
    
    return 0;
}