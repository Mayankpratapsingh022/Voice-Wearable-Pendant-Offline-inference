#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VOICE_LINK_VERSION 1
#define VOICE_LINK_HEADER_SIZE 12
#define VOICE_LINK_MAX_PAYLOAD 192

typedef enum {
    VOICE_LINK_CONTROL = 0x01,
    VOICE_LINK_MIC_OPUS = 0x02,
    VOICE_LINK_SPEAKER_OPUS = 0x03,
    VOICE_LINK_TELEMETRY = 0x04,
} voice_link_kind_t;

typedef enum {
    VOICE_CONTROL_CAPTURE_STARTED = 0x01,
    VOICE_CONTROL_CAPTURE_STOPPED = 0x02,
    VOICE_CONTROL_PROCESSING = 0x03,
    VOICE_CONTROL_SPEAKING = 0x04,
    VOICE_CONTROL_SAVED = 0x05,
    VOICE_CONTROL_IDLE = 0x06,
    VOICE_CONTROL_ERROR = 0x07,
} voice_control_opcode_t;

typedef struct {
    uint8_t version;
    voice_link_kind_t kind;
    uint16_t sequence;
    uint32_t timestamp_ms;
    uint16_t payload_size;
    uint8_t mouth_level;
    uint8_t flags;
} voice_link_header_t;

typedef struct {
    voice_link_header_t header;
    const uint8_t *payload;
} voice_link_packet_t;

bool voice_link_encode(uint8_t *out, size_t out_size, const voice_link_packet_t *packet, size_t *encoded_size);
bool voice_link_decode(const uint8_t *data, size_t data_size, voice_link_packet_t *packet);
