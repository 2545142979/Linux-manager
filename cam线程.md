```c
int thread_cam()
{
    unsigned int w = 640, h = 480, index = -1;
    unsigned int size = -1, ismjpeg = -1;
    char *jpg;

    //1.初始化摄像头
    int camera_fd = camera_init("/dev/video0",&w,&h,&size,&ismjpeg);
    if(-1 == camera_fd){
        return -1;
    }
    //2.启动摄像头
    if(-1 == camera_start(camera_fd)){
        return -1;
    }
    //出队8张图片丢弃
    for(int i = 0; i < 8; i++){
        camera_dqbuf(camera_fd, (void **)&jpg, &size, &index);
        camera_eqbuf(camera_fd, index);
    }
    
    while(1)
    {
        // 获取有效帧（注意强制转换）
        camera_dqbuf(camera_fd, (void **)&jpg, &size, &index);

        int img_fd = open("1.jpg", O_WRONLY | O_CREAT, 0640);
        write(img_fd, jpg ,size);
        close(img_fd);

        camera_eqbuf(camera_fd, index);
        usleep(33333);
    }
    
    camera_stop(camera_fd);
    return 0;
}
```

