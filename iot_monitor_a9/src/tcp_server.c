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

#define TCP_TEXT_BUFFER_SIZE 512U

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
        empty[0] = PROTOCOL_TAG_ENV;
        empty[1] = PROTOCOL_DEFAULT_DEVICE_ID;
        protocol_write_le16(empty + 2, PROTOCOL_PACKET_SIZE);
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

static int process_text_command_line(int connfd,
                                     struct shared_state *shared,
                                     struct serial_context *serial,
                                     char *command)
{
    trim_text_command(command);
    if (command[0] == '\0') {
        return 0;
    }

    if (strcmp(command, "pic") == 0) {
        handle_picture_request(connfd, shared);
        return 0;
    }

    if (strcmp(command, "env") == 0) {
        handle_env_request(connfd, shared);
        return 0;
    }

    handle_text_command(connfd, serial, command);
    return 0;
}

static int process_client_buffer(struct tcp_client_args *client, uint8_t *buffer, size_t *buffer_len)
{
    for (;;) {
        size_t newline_index;
        char *newline;

        if (*buffer_len >= PROTOCOL_PACKET_SIZE
            && protocol_is_command_packet(buffer, PROTOCOL_PACKET_SIZE)) {
            if (serial_send_bytes(client->serial, buffer, PROTOCOL_PACKET_SIZE) != 0) {
                write_all(client->connfd, "serial_error\n", 13);
            } else {
                write_all(client->connfd, "ok\n", 3);
            }

            memmove(buffer, buffer + PROTOCOL_PACKET_SIZE, *buffer_len - PROTOCOL_PACKET_SIZE);
            *buffer_len -= PROTOCOL_PACKET_SIZE;
            continue;
        }

        newline = memchr(buffer, '\n', *buffer_len);
        if (newline == NULL) {
            return 0;
        }

        newline_index = (size_t)(newline - (char *)buffer);
        buffer[newline_index] = '\0';
        process_text_command_line(client->connfd,
                                  client->shared,
                                  client->serial,
                                  (char *)buffer);

        memmove(buffer, buffer + newline_index + 1, *buffer_len - newline_index - 1);
        *buffer_len -= newline_index + 1;
    }
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
    uint8_t command_buffer[TCP_TEXT_BUFFER_SIZE];
    size_t command_len = 0;

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

        if (command_len + (size_t)ret > sizeof(command_buffer)) {
            fprintf(stderr, "client buffer overflow, dropping buffered data\n");
            command_len = 0;
        }

        if ((size_t)ret > sizeof(command_buffer) - command_len) {
            fprintf(stderr, "client message too large, dropping chunk\n");
            continue;
        }

        memcpy(command_buffer + command_len, buf, (size_t)ret);
        command_len += (size_t)ret;

        if (process_client_buffer(client, command_buffer, &command_len) != 0) {
            break;
        }

        if (command_len == 3 && memcmp(command_buffer, "pic", 3) == 0) {
            handle_picture_request(client->connfd, client->shared);
            command_len = 0;
            continue;
        }

        if (command_len == 3 && memcmp(command_buffer, "env", 3) == 0) {
            handle_env_request(client->connfd, client->shared);
            command_len = 0;
            continue;
        }

        if ((command_len == 6 && memcmp(command_buffer, "led_on", 6) == 0)
            || (command_len == 7 && memcmp(command_buffer, "led_off", 7) == 0)
            || (command_len == 7 && memcmp(command_buffer, "beep_on", 7) == 0)
            || (command_len == 8 && memcmp(command_buffer, "beep_off", 8) == 0)
            || (command_len == 6 && memcmp(command_buffer, "fan_on", 6) == 0)
            || (command_len == 7 && memcmp(command_buffer, "fan_off", 7) == 0)) {
            command_buffer[command_len] = '\0';
            handle_text_command(client->connfd, client->serial, (char *)command_buffer);
            command_len = 0;
        }
    }

    close(client->connfd);
    free(client);
    return NULL;
}
