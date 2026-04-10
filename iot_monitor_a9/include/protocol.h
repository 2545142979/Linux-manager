#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define PROTOCOL_PACKET_SIZE 36U
#define PROTOCOL_COMMAND_PREFIX_SIZE 5U
#define PROTOCOL_DEFAULT_DEVICE_ID 0x03U

#define PROTOCOL_TAG_ENV 0xBBU
#define PROTOCOL_TAG_CMD 0xDDU
#define PROTOCOL_TAG_CARD 0xCCU

enum control_opcode {
    CONTROL_LED_ON = 0x00,
    CONTROL_LED_OFF = 0x01,
    CONTROL_BUZ_ON = 0x02,
    CONTROL_BUZ_OFF = 0x03,
    CONTROL_FAN_ON = 0x04,
    CONTROL_FAN_OFF = 0x08
};

struct env_data {
    uint8_t device_id;
    uint16_t temperature;
    uint16_t humidity;
    uint32_t acceleration;
    uint64_t adc;
    uint32_t light;
    uint32_t state;
    uint32_t crc;
    uint8_t raw[PROTOCOL_PACKET_SIZE];
    int valid;
};

uint16_t protocol_read_le16(const uint8_t *buf);
uint32_t protocol_read_le32(const uint8_t *buf);
uint64_t protocol_read_le64(const uint8_t *buf);
void protocol_write_le16(uint8_t *buf, uint16_t value);
int protocol_is_command_packet(const uint8_t *buf, size_t len);
int protocol_parse_env_packet(const uint8_t *buf, size_t len, struct env_data *out);
int protocol_build_control_packet(uint8_t device_id, uint8_t opcode, uint8_t *out, size_t out_len);
int protocol_opcode_from_text(const char *text, uint8_t *opcode);

#endif
