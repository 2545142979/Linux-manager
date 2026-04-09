#include "Server.h"

int Server_Init(int port)
{
    // 1、创建套接字 (IPv4, TCP协议)
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(-1 == sockfd){
        perror("socket");
        return -1;
    }
    printf("sockfd: %d\n", sockfd);

    // 2、绑定 (Bind)
    struct sockaddr_in addr;                // 定义 Internet 协议地址结构体变量
    addr.sin_family      = AF_INET;         // 协议族：IPv4
    addr.sin_port        = htons(port);     // 端口号：9527 (主机字节序转网络字节序)
    addr.sin_addr.s_addr = inet_addr("0.0.0.0"); // 监听本机所有可用 IP

    if(-1 == bind(sockfd, (struct sockaddr *)&addr, sizeof(addr))){
        perror("bind");
        close(sockfd);
        return -1;
    }

    // 3、监听 (Listen)
    if(-1 == listen(sockfd, 12)){
        perror("listen");
        close(sockfd);
        return -1;
    }

    printf("server init success ... \n");
    return sockfd;
}

int wait_client(int listenfd)
{
    printf("wait for a new client ... \n");
    //4、阻塞等待，接受客户端连接
    struct sockaddr_in caddr;
    socklen_t len = sizeof(caddr);
    int connfd = accept(listenfd, (struct sockaddr *)&caddr, &len);
    if(-1 == connfd){
        perror("accept");
        return -1;
    }

    //打印连接成功的客户端ip和端口信息
    printf("client: %s, %u\n", inet_ntoa(caddr.sin_addr), ntohs(caddr.sin_port));
    return connfd;                //返回连接套接字
}