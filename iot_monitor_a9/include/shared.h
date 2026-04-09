#ifndef SHARED_H
#define SHARED_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

#define JPEG_BUFFER_SIZE (1024U * 1024U)

struct image_frame {
    uint8_t data[JPEG_BUFFER_SIZE];
    size_t size;
    unsigned long sequence;
    int ready;
};

struct shared_state {
    pthread_mutex_t image_mutex;
    pthread_mutex_t env_mutex;
    struct image_frame image;
    struct env_data env;
};

int shared_state_init(struct shared_state *state);
void shared_state_destroy(struct shared_state *state);
int shared_state_store_image(struct shared_state *state, const uint8_t *data, size_t size);
int shared_state_copy_image(struct shared_state *state, uint8_t *out, size_t out_size, size_t *image_size);
void shared_state_store_env(struct shared_state *state, const struct env_data *env);
int shared_state_copy_env(struct shared_state *state, struct env_data *env);

#endif
