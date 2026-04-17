#ifndef IMP_IPC_H
#define IMP_IPC_H

#define IMP_SOCKET_PATH "/tmp/imp_broker.sock"

int ipc_send_message(const char* source_module, const char* level, const char* message);

#endif