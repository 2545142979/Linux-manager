#ifndef SERIAL_PORT_H
#define SERIAL_PORT_H

#include <limits.h>
#include <pthread.h>
#include <termios.h>

#include "shared.h"

struct serial_context {
    int fd;
    pthread_mutex_t io_mutex;
    char device_path[PATH_MAX];
    speed_t baud_rate;
    uint8_t device_id;
};

struct serial_thread_args {
    struct serial_context *serial;
    struct shared_state *shared;
};

int serial_context_init(struct serial_context *ctx, const char *device_path, speed_t baud_rate);
void serial_context_destroy(struct serial_context *ctx);
int serial_send_bytes(struct serial_context *ctx, const uint8_t *buf, size_t len);
int serial_send_command_packet(struct serial_context *ctx, const uint8_t *buf, size_t len);
int serial_send_command_text(struct serial_context *ctx, const char *text);
void *serial_thread(void *arg);

#endif
