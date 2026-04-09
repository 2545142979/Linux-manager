#include "shared.h"

#include <string.h>

int shared_state_init(struct shared_state *state)
{
    if (state == NULL) {
        return -1;
    }

    memset(state, 0, sizeof(*state));

    if (pthread_mutex_init(&state->image_mutex, NULL) != 0) {
        return -1;
    }

    if (pthread_mutex_init(&state->env_mutex, NULL) != 0) {
        pthread_mutex_destroy(&state->image_mutex);
        return -1;
    }

    return 0;
}

void shared_state_destroy(struct shared_state *state)
{
    if (state == NULL) {
        return;
    }

    pthread_mutex_destroy(&state->image_mutex);
    pthread_mutex_destroy(&state->env_mutex);
}

int shared_state_store_image(struct shared_state *state, const uint8_t *data, size_t size)
{
    if (state == NULL || data == NULL || size == 0 || size > JPEG_BUFFER_SIZE) {
        return -1;
    }

    pthread_mutex_lock(&state->image_mutex);
    memcpy(state->image.data, data, size);
    state->image.size = size;
    state->image.sequence++;
    state->image.ready = 1;
    pthread_mutex_unlock(&state->image_mutex);

    return 0;
}

int shared_state_copy_image(struct shared_state *state, uint8_t *out, size_t out_size, size_t *image_size)
{
    int ready;

    if (state == NULL || out == NULL || image_size == NULL) {
        return -1;
    }

    pthread_mutex_lock(&state->image_mutex);
    ready = state->image.ready;
    if (ready && state->image.size <= out_size) {
        memcpy(out, state->image.data, state->image.size);
        *image_size = state->image.size;
    }
    pthread_mutex_unlock(&state->image_mutex);

    if (!ready) {
        return -1;
    }

    return 0;
}

void shared_state_store_env(struct shared_state *state, const struct env_data *env)
{
    if (state == NULL || env == NULL) {
        return;
    }

    pthread_mutex_lock(&state->env_mutex);
    state->env = *env;
    pthread_mutex_unlock(&state->env_mutex);
}

int shared_state_copy_env(struct shared_state *state, struct env_data *env)
{
    if (state == NULL || env == NULL) {
        return -1;
    }

    pthread_mutex_lock(&state->env_mutex);
    *env = state->env;
    pthread_mutex_unlock(&state->env_mutex);

    return env->valid ? 0 : -1;
}
