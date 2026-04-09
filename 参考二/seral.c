#include "serial.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

int fd;

void serial_init(int fd)
{
    //设置串口协议：波特率：115200、数据位：8、停止位：1、校验：无、流控：无
    struct termios options;        //定义结构体变量
    tcgetattr(fd, &options);       //获取fd对应设备文件的属性到 options结构体中

    cfsetispeed(&options, B115200); //设置设备输入input的速率
    cfsetospeed(&options, B115200); //设置设备输出output的速率

    options.c_cflag |= (CLOCAL | CREAD); // 本地连接 & 允许接收
    options.c_cflag &= ~PARENB;          // 无校验
    options.c_cflag &= ~CSTOPB;          // 1停止位
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;              // 8数据位
    options.c_cflag &= ~CRTSCTS;         // 无硬件流控

    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); // 原始模式
    tcsetattr(fd, TCSANOW, &options);    //设置fd对应的设备文件的属性为 options中的数据
}

int serial_cmd(char *cmd)
{
    unsigned char buf[36] = {0xdd, 0x0a, 0x24, 0x00, 0x00};

    if(strncmp(cmd, "led_on", 6) == 0){
        buf[4] = 0x0;
    } else if(strncmp(cmd, "led_off", 7) == 0){
        buf[4] = 0x1;
    } else if(strncmp(cmd, "beep_on", 7) == 0){
        buf[4] = 0x2;
    } else if(strncmp(cmd, "beep_off", 8) == 0){
        buf[4] = 0x3;
    }
    
    write(fd, buf, 36);
    return 0; 
}

void *serial_thread(void *arg)
{
    // 1、打开串口设备文件
    fd = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY);
    if( -1 == fd){
        perror("serial");
        return (void *)-1;
    }
    
    // 2、串口初始化
    serial_init(fd);

    // 3、数据读写
    while(1){
        char buf[36] = {0};
        int ret = read(fd, buf, 1);
        if(ret < 0){
            perror("read");
            break;
        }
        
        if((buf[0]&0xff) != 0xBB){  //判断数据第一字节是否为 0xBB,不是则数据不对
            continue;
        }
        
        read(fd, buf+1, 35);

        pthread_mutex_lock(&serial_mutex);
        // 解析环境信息数据 (注意：原图中这里被注释了，实际运行时若需要数据请取消注释)
        // env.light = (buf[20] << 24) + (buf[21] << 16) + (buf[22] << 8) + buf[23];
        // env.temp = ((int)buf[4] << 16) | buf[5];
        // env.hum = ((int)buf[6] << 16) | buf[7];
        pthread_mutex_unlock(&serial_mutex);

        for(int i=0; i < 36; i++)
            printf("%.2x ", buf[i] & 0xff);
        printf("\n");
    }
    
    return NULL;
}