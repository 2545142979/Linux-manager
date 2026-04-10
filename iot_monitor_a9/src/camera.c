#include "camera.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>

struct camera_buffer {
    void *start;
    size_t length;
};

struct camera_runtime {
    int fd;
    struct camera_buffer buffers[4];
    struct v4l2_requestbuffers reqbufs;
};

static int camera_open_device(const char *device_path)
{
    int fd = open(device_path, O_RDWR);
    if (fd < 0) {
        perror("open camera");
    }
    return fd;
}

static int camera_require_mjpeg(int fd, unsigned int *width, unsigned int *height)
{
    struct v4l2_capability cap;
    struct v4l2_format fmt;

    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) != 0) {
        perror("VIDIOC_QUERYCAP");
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) || !(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "camera does not support capture/streaming\n");
        return -1;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = *width;
    fmt.fmt.pix.height = *height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) != 0) {
        perror("VIDIOC_S_FMT");
        fprintf(stderr, "this project requires MJPEG camera output\n");
        return -1;
    }

    *width = fmt.fmt.pix.width;
    *height = fmt.fmt.pix.height;
    return 0;
}

static int camera_prepare_buffers(struct camera_runtime *rt)
{
    unsigned int i;

    memset(&rt->reqbufs, 0, sizeof(rt->reqbufs));
    rt->reqbufs.count = 4;
    rt->reqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rt->reqbufs.memory = V4L2_MEMORY_MMAP;

    if (ioctl(rt->fd, VIDIOC_REQBUFS, &rt->reqbufs) != 0) {
        perror("VIDIOC_REQBUFS");
        return -1;
    }

    for (i = 0; i < rt->reqbufs.count; ++i) {
        struct v4l2_buffer buf;

        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(rt->fd, VIDIOC_QUERYBUF, &buf) != 0) {
            perror("VIDIOC_QUERYBUF");
            return -1;
        }

        rt->buffers[i].length = buf.length;
        rt->buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, rt->fd, buf.m.offset);
        if (rt->buffers[i].start == MAP_FAILED) {
            perror("mmap");
            return -1;
        }

        if (ioctl(rt->fd, VIDIOC_QBUF, &buf) != 0) {
            perror("VIDIOC_QBUF");
            return -1;
        }
    }

    return 0;
}

static int camera_start_stream(int fd)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) != 0) {
        perror("VIDIOC_STREAMON");
        return -1;
    }
    return 0;
}

static int camera_wait_frame(int fd)
{
    fd_set fds;
    struct timeval timeout;
    int ret;

    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;

    ret = select(fd + 1, &fds, NULL, NULL, &timeout);
    if (ret < 0) {
        if (errno == EINTR) {
            return 1;
        }
        perror("select camera");
        return -1;
    }
    if (ret == 0) {
        fprintf(stderr, "camera frame timeout\n");
        return -1;
    }

    return 0;
}

static int camera_capture_one(struct camera_runtime *rt, void **data, size_t *size, unsigned int *index)
{
    struct v4l2_buffer buf;
    int ret;

    ret = camera_wait_frame(rt->fd);
    if (ret != 0) {
        return ret;
    }

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(rt->fd, VIDIOC_DQBUF, &buf) != 0) {
        perror("VIDIOC_DQBUF");
        return -1;
    }

    *data = rt->buffers[buf.index].start;
    *size = buf.bytesused;
    *index = buf.index;
    return 0;
}

static int camera_requeue(struct camera_runtime *rt, unsigned int index)
{
    struct v4l2_buffer buf;

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = index;

    if (ioctl(rt->fd, VIDIOC_QBUF, &buf) != 0) {
        perror("VIDIOC_QBUF requeue");
        return -1;
    }

    return 0;
}

static void camera_cleanup(struct camera_runtime *rt)
{
    unsigned int i;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (rt->fd >= 0) {
        ioctl(rt->fd, VIDIOC_STREAMOFF, &type);
    }

    for (i = 0; i < rt->reqbufs.count && i < 4; ++i) {
        if (rt->buffers[i].start != NULL && rt->buffers[i].start != MAP_FAILED) {
            munmap(rt->buffers[i].start, rt->buffers[i].length);
        }
    }

    if (rt->fd >= 0) {
        close(rt->fd);
    }
}

static void camera_sleep_ms(unsigned int milliseconds)
{
    struct timespec ts;

    ts.tv_sec = (time_t)(milliseconds / 1000U);
    ts.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
    nanosleep(&ts, NULL);
}

void *camera_thread(void *arg)
{
    struct camera_thread_args *thread_args = arg;
    struct camera_runtime rt;
    unsigned int i;

    if (thread_args == NULL) {
        return NULL;
    }

    memset(&rt, 0, sizeof(rt));
    rt.fd = -1;

    rt.fd = camera_open_device(thread_args->device_path);
    if (rt.fd < 0) {
        return NULL;
    }

    if (camera_require_mjpeg(rt.fd, &thread_args->width, &thread_args->height) != 0) {
        camera_cleanup(&rt);
        return NULL;
    }

    if (camera_prepare_buffers(&rt) != 0) {
        camera_cleanup(&rt);
        return NULL;
    }

    if (camera_start_stream(rt.fd) != 0) {
        camera_cleanup(&rt);
        return NULL;
    }

    for (i = 0; i < thread_args->warmup_frames; ++i) {
        void *data = NULL;
        size_t size = 0;
        unsigned int index = 0;

        if (camera_capture_one(&rt, &data, &size, &index) != 0) {
            camera_cleanup(&rt);
            return NULL;
        }
        if (camera_requeue(&rt, index) != 0) {
            camera_cleanup(&rt);
            return NULL;
        }
    }

    printf("camera ready: %s %ux%u\n",
           thread_args->device_path, thread_args->width, thread_args->height);

    for (;;) {
        void *data = NULL;
        size_t size = 0;
        unsigned int index = 0;

        if (camera_capture_one(&rt, &data, &size, &index) != 0) {
            break;
        }

        if (shared_state_store_image(thread_args->shared, data, size) != 0) {
            fprintf(stderr, "drop oversized jpeg frame: %zu bytes\n", size);
        }

        if (camera_requeue(&rt, index) != 0) {
            break;
        }

        if (thread_args->capture_interval_ms > 0) {
            camera_sleep_ms(thread_args->capture_interval_ms);
        }
    }

    camera_cleanup(&rt);
    return NULL;
}
