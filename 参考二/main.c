#include "server.h"

#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

#include "cam.h"
#include "serial.h"

pthread_mutex_t cam_mutex;      // 定义全局的互斥锁
pthread_mutex_t serial_mutex;

struct jpg_buf_t jpg = {0}; // 定义全局结构体变量 jpg
struct env_buf_t env = {0};

void *thread(void *arg);

int main()
{
    // 初始化互斥锁
    pthread_mutex_init(&cam_mutex, NULL);
    pthread_mutex_init(&serial_mutex, NULL);

    pthread_t tid;
    // 1、开启摄像头线程
    pthread_create(&tid, NULL, cam_thread, NULL);
    pthread_detach(tid);

    // 2、开启串口的线程
    pthread_create(&tid, NULL, serial_thread, NULL);
    pthread_detach(tid);

    // 3、服务器初始化
    int sockfd = server_init(9527);
    if(-1 == sockfd)
        return -1;
    
    // 4、
    while(1){
        // 2、接受客户端连接
        int connfd = wait_client(sockfd);
        if(-1 == connfd)
            continue;
        
        pthread_create(&tid, NULL, thread, &connfd);            //开启子线程
        pthread_detach(tid);                                    //线程分离，回收资源
    }
}

void *thread(void *arg)
{
    int connfd = *(int *)arg;
    while(1){
        char buf[36] = {0};
        int ret = read(connfd, buf, sizeof(buf));    // 阻塞等待读取客户端数据
        if(-1 == ret){
            perror("read");
            return (void *)-1;
        } else if(0 == ret) {
            printf("client leaved...\n");
            break;
        }
        
        if(strncmp(buf, "pic", 3) == 0){
            
            char len[7] = {0};
            
            pthread_mutex_lock(&cam_mutex);           // 加锁

            sprintf(len, "%d", jpg.jpg_size);         // 取出长度，组装到 len
            printf("len: %s\n", len);

            write(connfd, len, 7);                    // 发送图片长度
            int ret = write(connfd, jpg.jpg_buf, jpg.jpg_size); // 发送图片内容
            printf("ret: %d\n", ret);

            pthread_mutex_unlock(&cam_mutex);

        } else if(strncmp(buf, "env", 3) == 0){
            pthread_mutex_lock(&serial_mutex);
            char response[32] = {0};
            sprintf(response, "t:%d,h:%d,l:%d", env.temp, env.hum, env.light);
            pthread_mutex_unlock(&serial_mutex);

            write(connfd, response, sizeof(response));

        } else {
            serial_cmd(buf);
        }
    }
}