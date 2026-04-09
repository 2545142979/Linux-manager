#include <stdio.h>
#include "Server.h"
#include <pthread.h>

void *thread(void *arg);

int main()
{
    //1、服务器初始化
    int sockfd = Server_Init(9527);
    if(-1 == sockfd)
        return -1;

    while(1){
        //2、接受客户端连接
        int connfd = wait_client(sockfd);
        if(-1 == connfd)
            continue;

        pthread_t tid;
        pthread_create(&tid, NULL, thread, &connfd); //开启线程
        pthread_detach(tid); //分离线程，让其退出后自动回收资源
    }
}

void *thread(void *arg)
{
    int connfd = *(int *)arg;
    while(1){
        char buf[128] = {0};
        int ret = read(connfd, buf, sizeof(buf)); //阻塞等待，读取客户端数据
        
        if(-1 == ret){
            perror("read");
            return (void *)-1;
        } else if(0 == ret) {
            printf("client leaved...\n");
            break;
        }
        
        printf("recv: %d bytes, %s\n", ret, buf);
    }
    close(connfd); // 建议在这里加上关闭套接字的操作
}
