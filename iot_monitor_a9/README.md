# IoT Monitor A9

这是一个基于 `ask.md` 落地的 A9 端物联网监控服务，负责把摄像头图像和 M0 串口环境数据统一转发给 Qt 客户端。

## 模块

- `src/camera.c`: V4L2 摄像头线程，采集最新 MJPEG 帧并更新共享缓冲区。
- `src/serial_port.c`: 串口线程，按 36 字节协议读取 `0xBB` 环境包并刷新共享状态。
- `src/tcp_server.c`: TCP 业务线程，响应图片请求、环境请求和控制命令转发。
- `src/protocol.c`: 协议编解码和控制指令映射。
- `src/shared.c`: 图片与环境数据的线程安全共享区。

## 构建

推荐使用 CMake。

```bash
./scripts/build_linux.sh
```

如果要使用 `arm-linux-gnueabi-gcc` 交叉编译:

```bash
./scripts/build_arm_linux_gcc.sh
```

如果交叉编译器不在 `PATH` 中，可以先指定:

```bash
export ARM_LINUX_GCC_COMPILER=/path/to/arm-linux-gnueabi-gcc
./scripts/build_arm_linux_gcc.sh
```

## 运行

本地构建产物默认位于 `build/linux/iot_monitor_server`，ARM 构建产物默认位于 `build/arm-linux-gcc/iot_monitor_server`。

```bash
./build/linux/iot_monitor_server [tcp_port] [serial_device] [camera_device]
```

默认参数:

- TCP 端口: `9527`
- 串口设备: `/dev/ttyUSB0`
- 摄像头设备: `/dev/video0`

## TCP 约定

- 发送 `pic`
  服务端先返回 7 位 ASCII 图片长度，再返回 JPEG 数据。
- 发送 `env`
  服务端返回最近一次收到的 36 字节环境包原始数据。
- 发送 36 字节 `0xDD` 命令包
  服务端直接转发到串口。
- 发送文本命令
  支持 `led_on`、`led_off`、`buz_on`、`buz_off`、`fan_on`、`fan_off`，服务端会组装成 36 字节命令包后发给 M0。

## 说明

- 当前摄像头线程要求设备支持 `MJPEG` 输出，这样可以直接把最新帧发送给 Qt。
- `crc` 字段当前只做透传和解析，没有在 A9 侧重新计算或校验。
