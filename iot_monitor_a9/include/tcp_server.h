#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <stdint.h>

#include "serial_port.h"
#include "shared.h"

struct tcp_client_args {
    int connfd;
    struct shared_state *shared;
    struct serial_context *serial;
};

int tcp_server_listen(uint16_t port);
void *tcp_client_thread(void *arg);

#endif
