#ifndef SERIAL_H
#define SERIAL_H

#include <pthread.h>

struct env_buf_t{
    int light;
    int hum;
    int temp;
};

extern pthread_mutex_t serial_mutex;
extern struct env_buf_t env;

void serial_init(int fd);
int serial_cmd(char *cmd);
void *serial_thread(void *arg);

#endif