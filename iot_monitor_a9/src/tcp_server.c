#include "tcp_server.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "protocol.h"

static int write_all(int fd, const void *buf, size_t len)
{
    size_t total = 0;
    const uint8_t *bytes = buf;

    while (total < len) {
        ssize_t ret = write(fd, bytes + total, len - total);
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

static void trim_text_command(char *buf)
{
    size_t len = strlen(buf);
    while (len > 0 && isspace((unsigned char)buf[len - 1])) {
        buf[len - 1] = '\0';
        --len;
    }
}

static void handle_picture_request(int connfd, struct shared_state *shared)
{
    uint8_t image[JPEG_BUFFER_SIZE];
    size_t image_size = 0;
    char len_buf[8] = {0};

    if (shared_state_copy_image(shared, image, sizeof(image), &image_size) != 0) {
        write_all(connfd, "0000000", 7);
        return;
    }

    snprintf(len_buf, sizeof(len_buf), "%07zu", image_size);
    if (write_all(connfd, len_buf, 7) != 0) {
        return;
    }
    write_all(connfd, image, image_size);
}

static void handle_env_request(int connfd, struct shared_state *shared)
{
    struct env_data env;
    uint8_t empty[PROTOCOL_PACKET_SIZE] = {0};

    if (shared_state_copy_env(shared, &env) != 0) {
        write_all(connfd, empty, sizeof(empty));
        return;
    }

    write_all(connfd, env.raw, sizeof(env.raw));
}

static void handle_text_command(int connfd, struct serial_context *serial, char *command)
{
    trim_text_command(command);

    if (serial_send_command_text(serial, command) == 0) {
        write_all(connfd, "ok\n", 3);
        return;
    }

    write_all(connfd, "unsupported\n", 12);
}

int tcp_server_listen(uint16_t port)
{
    int fd;
    int opt = 1;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
        perror("setsockopt");
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (listen(fd, 16) != 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    return fd;
}

void *tcp_client_thread(void *arg)
{
    struct tcp_client_args *client = arg;
    uint8_t buf[256];

    if (client == NULL) {
        return NULL;
    }

    for (;;) {
        ssize_t ret = read(client->connfd, buf, sizeof(buf));
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("read client");
            break;
        }
        if (ret == 0) {
            break;
        }

        if ((size_t)ret >= 3 && memcmp(buf, "pic", 3) == 0) {
            handle_picture_request(client->connfd, client->shared);
            continue;
        }

        if ((size_t)ret >= 3 && memcmp(buf, "env", 3) == 0) {
            handle_env_request(client->connfd, client->shared);
            continue;
        }

        if (protocol_is_command_packet(buf, (size_t)ret)) {
            if (serial_send_bytes(client->serial, buf, PROTOCOL_PACKET_SIZE) != 0) {
                write_all(client->connfd, "serial_error\n", 13);
            } else {
                write_all(client->connfd, "ok\n", 3);
            }
            continue;
        }

        buf[(size_t)ret < sizeof(buf) ? (size_t)ret : sizeof(buf) - 1] = '\0';
        handle_text_command(client->connfd, client->serial, (char *)buf);
    }

    close(client->connfd);
    free(client);
    return NULL;
}
