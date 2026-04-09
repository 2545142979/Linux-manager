#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <linux/videodev2.h>
#include "camera.h"

// 摄像头设备路径
#define CAM_DEV_PATH "/dev/video0"
// V4L2 申请缓冲区数量
#define REQBUFS_COUNT 4

// 全局外部变量声明（来自 main.c）
extern pthread_mutex_t cam_mutex;
extern unsigned int video_flag;

extern struct jpg_buf_t jpg;

// 摄像头缓冲区结构体
struct cam_buf {
    void *start;
    size_t length;
};

// V4L2 全局变量
static struct v4l2_requestbuffers reqbufs;
static struct cam_buf bufs[REQBUFS_COUNT];

/*******************************************
 * 底层 V4L2 摄像头操作函数
 ******************************************/
int camera_init(char *devpath, unsigned int *width, unsigned int *height, unsigned int *size, unsigned int *ismjpeg)
{
    int i;
    int fd = -1;
    int ret;
    struct v4l2_buffer vbuf;
    struct v4l2_format format;
    struct v4l2_capability capability;

    // 打开摄像头设备
    if((fd = open(devpath, O_RDWR)) == -1){
        perror("open camera failed");
        return -1;
    }

    // 查询设备能力
    ret = ioctl(fd, VIDIOC_QUERYCAP, &capability);
    if (ret == -1) {
        perror("VIDIOC_QUERYCAP failed");
        close(fd);
        return -1;
    }

    // 检查是否支持视频采集
    if(!(capability.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "device does not support video capture\n");
        close(fd);
        return -1;
    }

    // 检查是否支持流采集
    if(!(capability.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "device does not support streaming\n");
        close(fd);
        return -1;
    }

    // 优先设置 MJPEG 格式
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    format.fmt.pix.width = *width;
    format.fmt.pix.height = *height;
    format.fmt.pix.field = V4L2_FIELD_ANY;
    ret = ioctl(fd, VIDIOC_S_FMT, &format);
    if(ret == -1) {
        // MJPEG 不支持，尝试 YUYV
        memset(&format, 0, sizeof(format));
        format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        format.fmt.pix.width = *width;
        format.fmt.pix.height = *height;
        format.fmt.pix.field = V4L2_FIELD_ANY;
        ret = ioctl(fd, VIDIOC_S_FMT, &format);
        if(ret == -1) {
            perror("set video format failed");
            close(fd);
            return -1;
        }
        *ismjpeg = 0;
        fprintf(stdout, "camera format: YUYV\n");
    } else {
        *ismjpeg = 1;
        fprintf(stdout, "camera format: MJPEG\n");
    }

    // 获取实际设置的格式
    ret = ioctl(fd, VIDIOC_G_FMT, &format);
    if (ret == -1) {
        perror("get video format failed");
        close(fd);
        return -1;
    }

    // 申请内核缓冲区
    memset(&reqbufs, 0, sizeof(struct v4l2_requestbuffers));
    reqbufs.count   = REQBUFS_COUNT;
    reqbufs.type    = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbufs.memory  = V4L2_MEMORY_MMAP;
    ret = ioctl(fd, VIDIOC_REQBUFS, &reqbufs);
    if (ret == -1) {
        perror("request buffers failed");
        close(fd);
        return -1;
    }

    // 映射缓冲区到用户空间并入队
    for (i = 0; i < reqbufs.count; i++){
        memset(&vbuf, 0, sizeof(struct v4l2_buffer));
        vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        vbuf.memory = V4L2_MEMORY_MMAP;
        vbuf.index = i;
        ret = ioctl(fd, VIDIOC_QUERYBUF, &vbuf);
        if (ret == -1) {
            perror("query buffer failed");
            close(fd);
            return -1;
        }

        // 内存映射
        bufs[i].length = vbuf.length;
        bufs[i].start = mmap(NULL, vbuf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, vbuf.m.offset);
        if (bufs[i].start == MAP_FAILED) {
            perror("mmap failed");
            close(fd);
            return -1;
        }

        // 缓冲区入队
        ret = ioctl(fd, VIDIOC_QBUF, &vbuf);
        if (ret == -1) {
            perror("queue buffer failed");
            close(fd);
            return -1;
        }
    }

    // 返回实际参数
    *width = format.fmt.pix.width;
    *height = format.fmt.pix.height;
    *size = bufs[0].length;

    return fd;
}

// 启动摄像头采集
int camera_start(int fd)
{
    int ret;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(fd, VIDIOC_STREAMON, &type);
    if (ret == -1) {
        perror("start stream failed");
        return -1;
    }
    fprintf(stdout, "camera start capture success\n");
    return 0;
}

// 出队一帧数据
int camera_dqbuf(int fd, void **buf, unsigned int *size, unsigned int *index)
{
    int ret;
    fd_set fds;
    struct timeval timeout;
    struct v4l2_buffer vbuf;

    while (1) {
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;

        // 等待数据就绪
        ret = select(fd + 1, &fds, NULL, NULL, &timeout);
        if (ret == -1) {
            if (errno == EINTR)
                continue;
            perror("select failed");
            return -1;
        } else if (ret == 0) {
            fprintf(stderr, "dequeue buffer timeout\n");
            return -1;
        }

        // 出队缓冲区
        memset(&vbuf, 0, sizeof(vbuf));
        vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        vbuf.memory = V4L2_MEMORY_MMAP;
        ret = ioctl(fd, VIDIOC_DQBUF, &vbuf);
        if (ret == -1) {
            perror("dequeue buffer failed");
            return -1;
        }

        *buf = bufs[vbuf.index].start;
        *size = vbuf.bytesused;
        *index = vbuf.index;
        return 0;
    }
}

// 缓冲区入队
int camera_eqbuf(int fd, unsigned int index)
{
    int ret;
    struct v4l2_buffer vbuf;
    memset(&vbuf, 0, sizeof(vbuf));
    vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vbuf.memory = V4L2_MEMORY_MMAP;
    vbuf.index = index;
    ret = ioctl(fd, VIDIOC_QBUF, &vbuf);
    if (ret == -1) {
        perror("queue buffer failed");
        return -1;
    }
    return 0;
}

// 停止摄像头采集
int camera_stop(int fd)
{
    int ret;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
    if (ret == -1) {
        perror("stop stream failed");
        return -1;
    }
    fprintf(stdout, "camera stop capture success\n");
    return 0;
}

// 释放摄像头资源
int camera_exit(int fd)
{
    int i;
    // 取消内存映射
    for (i = 0; i < reqbufs.count; i++) {
        munmap(bufs[i].start, bufs[i].length);
    }
    close(fd);
    fprintf(stdout, "camera resource released\n");
    return 0;
}

/*******************************************
 * 摄像头采集线程函数（原 cam.c 核心逻辑）
 ******************************************/
void *thread_cam(void *connfd)
{
    int fd;
    unsigned int width = 640, height = 480;
    unsigned int size = 0;
    unsigned int index = 0;
    int ismjpeg = 0;
    char *jpg_data = NULL;

    // 1. 初始化摄像头
    fd = camera_init(CAM_DEV_PATH, &width, &height, &size, &ismjpeg);
    if (fd == -1) {
        printf("camera_init failed\n");
        pthread_exit(NULL);
    }

    // 强制要求 MJPG 格式
    if (!ismjpeg) {
        printf("Camera does not support MJPG format, exit.\n");
        camera_exit(fd);
        pthread_exit(NULL);
    }

    // 2. 启动采集
    if (camera_start(fd) == -1) {
        printf("camera_start failed\n");
        camera_exit(fd);
        pthread_exit(NULL);
    }

    // 3. 丢弃前8帧（消除启动脏数据）
    for (int i = 0; i < 8; i++) {
        camera_dqbuf(fd, (void **)&jpg_data, &size, &index);
        camera_eqbuf(fd, index);
    }
    printf("discard 8 frames, start capture...\n");

    // 4. 主循环采集
    while (1) {
        // 获取一帧
        if (camera_dqbuf(fd, (void **)&jpg_data, &size, &index) == -1) {
            printf("camera_dqbuf failed\n");
            break;
        }

        // 加锁拷贝 JPEG 数据到全局缓冲区
        pthread_mutex_lock(&cam_mutex);
        if (size <= sizeof(jpg.jpg_buf)) {
            memcpy(jpg.jpg_buf, jpg_data, size);
            jpg.jpg_size = size;
        } else {
            printf("JPEG size %u exceeds buffer size %zu, discard frame\n", size, sizeof(jpg.jpg_buf));
        }
        pthread_mutex_unlock(&cam_mutex);

        // 测试：保存为本地文件
        int fd1 = open("1.jpg", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd1 != -1) {
            write(fd1, jpg_data, size);
            close(fd1);
        }

        // 重新入队缓冲区
        if (camera_eqbuf(fd, index) == -1) {
            printf("camera_eqbuf failed\n");
            break;
        }

        // 控制帧率 20fps
        usleep(50000);
    }

    // 5. 资源释放
    camera_stop(fd);
    camera_exit(fd);
    printf("camera thread exited\n");
    pthread_exit(NULL);
}