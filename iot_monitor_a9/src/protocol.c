#include "protocol.h"

#include <string.h>

// 解析协议数据包的工具函数
// 协议数据包格式：
// 0: tag (1 byte) - PROTOCOL_TAG_CMD 或 PROTOCOL_TAG_ENV
// 1: device_id (1 byte) - PROTOCOL_DEVICE_ID
// 2-3: length (2 bytes, little-endian) - 数据包总长度（包含头部）
// 4-...: payload (根据 length 定义)

//read_le16、read_le32、read_le64 从小端字节序的缓冲区中读取整数值
//write_le16 将整数值写入小端字节序的缓冲区
//protocol_is_command_packet 检查缓冲区是否为有效的命令数据包
//protocol_parse_env_packet 解析环境数据包并填充 env_data 结构体
//protocol_build_control_packet 构建控制命令数据包
//protocol_opcode_from_text 将文本命令转换为对应的操作码
// 这些函数在 main.c 中被调用，用于处理接收到的数据包和构建响应数据包
// 例如，main.c 中的网络线程会调用 protocol_is_command_packet 来检查接收到的数据包是否为命令包，如果是，则调用 protocol_parse_env_packet 来解析环境数据，并根据解析结果执行相应的控制操作（如打开 LED、蜂鸣器等），最后使用 protocol_build_control_packet 构建响应数据包发送回客户端。
// 这些工具函数的实现相对简单，主要涉及字节序转换和数据包格式验证，核心逻辑在 main.c 中的网络线程处理函数中实现。

uint16_t protocol_read_le16(const uint8_t *buf)
{
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint16_t protocol_read_be16(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

uint32_t protocol_read_le32(const uint8_t *buf)
{
    return (uint32_t)buf[0]
        | ((uint32_t)buf[1] << 8)
        | ((uint32_t)buf[2] << 16)
        | ((uint32_t)buf[3] << 24);
}

uint64_t protocol_read_le64(const uint8_t *buf)
{
    return (uint64_t)buf[0]
        | ((uint64_t)buf[1] << 8)
        | ((uint64_t)buf[2] << 16)
        | ((uint64_t)buf[3] << 24)
        | ((uint64_t)buf[4] << 32)
        | ((uint64_t)buf[5] << 40)
        | ((uint64_t)buf[6] << 48)
        | ((uint64_t)buf[7] << 56);
}

void protocol_write_le16(uint8_t *buf, uint16_t value)
{
    buf[0] = (uint8_t)(value & 0xFFU);
    buf[1] = (uint8_t)((value >> 8) & 0xFFU);
}

int protocol_is_command_packet(const uint8_t *buf, size_t len)
{
    if (buf == NULL || len != PROTOCOL_PACKET_SIZE) {
        return 0;
    }

    return buf[0] == PROTOCOL_TAG_CMD
        && buf[1] == PROTOCOL_DEVICE_ID
        && protocol_read_le16(buf + 2) == PROTOCOL_PACKET_SIZE;
}

int protocol_parse_env_packet(const uint8_t *buf, size_t len, struct env_data *out)
{
    if (buf == NULL || out == NULL || len != PROTOCOL_PACKET_SIZE) {
        return -1;
    }

    if (buf[0] != PROTOCOL_TAG_ENV || protocol_read_le16(buf + 2) != PROTOCOL_PACKET_SIZE) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    // M0 实际上报的温湿度字段高字节在前，和其余小端字段不同。
    out->temperature = protocol_read_be16(buf + 4);
    out->humidity = protocol_read_be16(buf + 6);
    out->acceleration = protocol_read_le32(buf + 8);
    out->adc = protocol_read_le64(buf + 12);
    out->light = protocol_read_le32(buf + 20);
    out->state = protocol_read_le32(buf + 24);
    out->crc = protocol_read_le32(buf + 32);
    memcpy(out->raw, buf, PROTOCOL_PACKET_SIZE);
    out->valid = 1;

    return 0;
}

int protocol_build_control_packet(uint8_t opcode, uint8_t *out, size_t out_len)
{
    if (out == NULL || out_len < PROTOCOL_PACKET_SIZE) {
        return -1;
    }

    memset(out, 0, PROTOCOL_PACKET_SIZE);
    out[0] = PROTOCOL_TAG_CMD;
    out[1] = PROTOCOL_DEVICE_ID;
    protocol_write_le16(out + 2, PROTOCOL_PACKET_SIZE);
    out[4] = opcode;

    return 0;
}

int protocol_opcode_from_text(const char *text, uint8_t *opcode)
{
    if (text == NULL || opcode == NULL) {
        return -1;
    }

    if (strcmp(text, "led_on") == 0) {
        *opcode = CONTROL_LED_ON;
        return 0;
    }
    if (strcmp(text, "led_off") == 0) {
        *opcode = CONTROL_LED_OFF;
        return 0;
    }
    if (strcmp(text, "buz_on") == 0 || strcmp(text, "beep_on") == 0) {
        *opcode = CONTROL_BUZ_ON;
        return 0;
    }
    if (strcmp(text, "buz_off") == 0 || strcmp(text, "beep_off") == 0) {
        *opcode = CONTROL_BUZ_OFF;
        return 0;
    }
    if (strcmp(text, "fan_on") == 0) {
        *opcode = CONTROL_FAN_ON;
        return 0;
    }
    if (strcmp(text, "fan_off") == 0) {
        *opcode = CONTROL_FAN_OFF;
        return 0;
    }

    return -1;
}
