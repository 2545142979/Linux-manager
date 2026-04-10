#include "serial_port.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "protocol.h"

#ifndef CRTSCTS
#define CRTSCTS 0
#endif

static void serial_log_tx_packet(const char *source, const uint8_t *buf, size_t len)
{
    size_t i;

    if (source == NULL || buf == NULL || len == 0) {
        return;
    }

    printf("serial tx [%s] len=%zu:", source, len);
    for (i = 0; i < len; ++i) {
        printf(" %02X", buf[i]);
    }
    printf("\n");
}

static int serial_configure_fd(int fd, speed_t baud_rate)
{
    struct termios options;

    if (tcgetattr(fd, &options) != 0) {
        perror("tcgetattr");
        return -1;
    }

    cfsetispeed(&options, baud_rate);
    cfsetospeed(&options, baud_rate);
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag &= ~(tcflag_t)CRTSCTS;
    options.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    options.c_oflag &= ~OPOST;
    options.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    options.c_cc[VMIN] = 1;
    options.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        perror("tcsetattr");
        return -1;
    }

    return 0;
}

static int serial_open_locked(struct serial_context *ctx)
{
    int fd;

    fd = open(ctx->device_path, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        return -1;
    }

    if (serial_configure_fd(fd, ctx->baud_rate) != 0) {
        close(fd);
        return -1;
    }

    pthread_mutex_lock(&ctx->io_mutex);
    ctx->fd = fd;
    pthread_mutex_unlock(&ctx->io_mutex);

    printf("serial connected: %s\n", ctx->device_path);
    return 0;
}

static void serial_close_locked(struct serial_context *ctx)
{
    pthread_mutex_lock(&ctx->io_mutex);
    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
    pthread_mutex_unlock(&ctx->io_mutex);
}

static int read_exact_fd(int fd, uint8_t *buf, size_t len)
{
    size_t total = 0;

    while (total < len) {
        ssize_t ret = read(fd, buf + total, len - total);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (ret == 0) {
            return -1;
        }
        total += (size_t)ret;
    }

    return 0;
}

int serial_context_init(struct serial_context *ctx, const char *device_path, speed_t baud_rate)
{
    if (ctx == NULL || device_path == NULL) {
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->fd = -1;
    ctx->baud_rate = baud_rate;
    ctx->device_id = PROTOCOL_DEFAULT_DEVICE_ID;
    strncpy(ctx->device_path, device_path, sizeof(ctx->device_path) - 1);

    if (pthread_mutex_init(&ctx->io_mutex, NULL) != 0) {
        return -1;
    }

    return 0;
}

void serial_context_destroy(struct serial_context *ctx)
{
    if (ctx == NULL) {
        return;
    }

    serial_close_locked(ctx);
    pthread_mutex_destroy(&ctx->io_mutex);
}

int serial_send_bytes(struct serial_context *ctx, const uint8_t *buf, size_t len)
{
    size_t total = 0;

    if (ctx == NULL || buf == NULL || len == 0) {
        return -1;
    }

    pthread_mutex_lock(&ctx->io_mutex);
    if (ctx->fd < 0) {
        pthread_mutex_unlock(&ctx->io_mutex);
        return -1;
    }

    while (total < len) {
        ssize_t ret = write(ctx->fd, buf + total, len - total);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            pthread_mutex_unlock(&ctx->io_mutex);
            return -1;
        }
        total += (size_t)ret;
    }

    while (tcdrain(ctx->fd) != 0) {
        if (errno == EINTR) {
            continue;
        }
        pthread_mutex_unlock(&ctx->io_mutex);
        return -1;
    }

    pthread_mutex_unlock(&ctx->io_mutex);

    return 0;
}

int serial_send_command_packet(struct serial_context *ctx, const uint8_t *buf, size_t len)
{
    int ret;

    if (buf == NULL || len != PROTOCOL_PACKET_SIZE) {
        return -1;
    }

    ret = serial_send_bytes(ctx, buf, len);
    if (ret == 0) {
        serial_log_tx_packet("binary", buf, len);
    } else {
        fprintf(stderr, "serial tx failed [binary]\n");
    }

    return ret;
}

int serial_send_command_text(struct serial_context *ctx, const char *text)
{
    uint8_t opcode;
    uint8_t packet[PROTOCOL_PACKET_SIZE];
    int ret;

    if (protocol_opcode_from_text(text, &opcode) != 0) {
        return -1;
    }

    if (protocol_build_control_packet(PROTOCOL_DEFAULT_DEVICE_ID, opcode, packet, sizeof(packet)) != 0) {
        return -1;
    }

    ret = serial_send_command_packet(ctx, packet, sizeof(packet));
    if (ret == 0) {
        printf("serial tx command text=%s opcode=0x%02X dev=0x%02X\n",
               text, opcode, PROTOCOL_DEFAULT_DEVICE_ID);
    }

    return ret;
}

void *serial_thread(void *arg)
{
    struct serial_thread_args *thread_args = arg;
    struct serial_context *serial;
    struct shared_state *shared;

    if (thread_args == NULL) {
        return NULL;
    }

    serial = thread_args->serial;
    shared = thread_args->shared;

    for (;;) {
        uint8_t packet[PROTOCOL_PACKET_SIZE];
        struct env_data env;
        int fd;

        pthread_mutex_lock(&serial->io_mutex);
        fd = serial->fd;
        pthread_mutex_unlock(&serial->io_mutex);

        if (fd < 0) {
            if (serial_open_locked(serial) != 0) {
                perror("open serial");
                sleep(1);
                continue;
            }
            continue;
        }

        if (read_exact_fd(fd, packet, 1) != 0) {
            perror("read serial header");
            serial_close_locked(serial);
            sleep(1);
            continue;
        }

        if (packet[0] != PROTOCOL_TAG_ENV) {
            continue;
        }

        if (read_exact_fd(fd, packet + 1, PROTOCOL_PACKET_SIZE - 1) != 0) {
            perror("read serial packet");
            serial_close_locked(serial);
            sleep(1);
            continue;
        }

        if (protocol_parse_env_packet(packet, sizeof(packet), &env) != 0) {
            fprintf(stderr, "ignore invalid environment packet\n");
            continue;
        }

        shared_state_store_env(shared, &env);
        printf("env updated: dev=0x%02x temp=%u hum=%u light=%u state=0x%08x\n",
               env.device_id, env.temperature, env.humidity, env.light, env.state);
    }

    return NULL;
}
