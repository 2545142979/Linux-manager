#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "camera.h"
#include "serial_port.h"
#include "shared.h"
#include "tcp_server.h"

static uint16_t parse_port(const char *text)
{
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);

    if (text == NULL || *text == '\0' || end == NULL || *end != '\0' || value > 65535UL) {
        return 9527U;
    }

    return (uint16_t)value;
}

int main(int argc, char *argv[])
{
    const char *serial_device = "/dev/ttyUSB0";
    const char *camera_device = "/dev/video0";
    uint16_t port = 9527U;
    int listenfd;
    struct shared_state shared;
    struct serial_context serial;
    struct serial_thread_args serial_args;
    struct camera_thread_args camera_args;
    pthread_t serial_tid;
    pthread_t camera_tid;

    if (argc > 1) {
        port = parse_port(argv[1]);
    }
    if (argc > 2) {
        serial_device = argv[2];
    }
    if (argc > 3) {
        camera_device = argv[3];
    }

    if (shared_state_init(&shared) != 0) {
        fprintf(stderr, "failed to initialize shared state\n");
        return 1;
    }

    if (serial_context_init(&serial, serial_device, B115200) != 0) {
        fprintf(stderr, "failed to initialize serial context\n");
        shared_state_destroy(&shared);
        return 1;
    }

    serial_args.serial = &serial;
    serial_args.shared = &shared;

    camera_args.shared = &shared;
    camera_args.device_path = camera_device;
    camera_args.width = 640;
    camera_args.height = 480;
    camera_args.warmup_frames = 8;
    camera_args.capture_interval_ms = 50;

    if (pthread_create(&serial_tid, NULL, serial_thread, &serial_args) != 0) {
        fprintf(stderr, "failed to start serial thread\n");
        serial_context_destroy(&serial);
        shared_state_destroy(&shared);
        return 1;
    }
    pthread_detach(serial_tid);

    if (pthread_create(&camera_tid, NULL, camera_thread, &camera_args) != 0) {
        fprintf(stderr, "failed to start camera thread\n");
        serial_context_destroy(&serial);
        shared_state_destroy(&shared);
        return 1;
    }
    pthread_detach(camera_tid);

    listenfd = tcp_server_listen(port);
    if (listenfd < 0) {
        serial_context_destroy(&serial);
        shared_state_destroy(&shared);
        return 1;
    }

    printf("iot monitor server listening on 0.0.0.0:%u\n", port);
    printf("serial device: %s\n", serial_device);
    printf("camera device: %s\n", camera_device);

    for (;;) {
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);
        int connfd = accept(listenfd, (struct sockaddr *)&addr, &addr_len);
        pthread_t client_tid;
        struct tcp_client_args *client_args;

        if (connfd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            continue;
        }

        printf("client connected: %s:%u\n",
               inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

        client_args = malloc(sizeof(*client_args));
        if (client_args == NULL) {
            perror("malloc client args");
            close(connfd);
            continue;
        }

        client_args->connfd = connfd;
        client_args->shared = &shared;
        client_args->serial = &serial;

        if (pthread_create(&client_tid, NULL, tcp_client_thread, client_args) != 0) {
            perror("pthread_create client");
            close(connfd);
            free(client_args);
            continue;
        }

        pthread_detach(client_tid);
    }

    close(listenfd);
    serial_context_destroy(&serial);
    shared_state_destroy(&shared);
    return 0;
}
