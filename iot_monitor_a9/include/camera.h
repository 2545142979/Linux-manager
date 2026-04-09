#ifndef CAMERA_H
#define CAMERA_H

#include "shared.h"

struct camera_thread_args {
    struct shared_state *shared;
    const char *device_path;
    unsigned int width;
    unsigned int height;
    unsigned int warmup_frames;
    unsigned int capture_interval_ms;
};

void *camera_thread(void *arg);

#endif
