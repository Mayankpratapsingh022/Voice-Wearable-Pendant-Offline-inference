#include "voice_link.h"

#include <string.h>

static void write_u16_le(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void write_u32_le(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static uint16_t read_u16_le(const uint8_t *in) {
    return (uint16_t)in[0] | ((uint16_t)in[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *in) {
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static bool is_known_kind(voice_link_kind_t kind) {
    return kind == VOICE_LINK_CONTROL || kind == VOICE_LINK_MIC_OPUS || kind == VOICE_LINK_SPEAKER_OPUS ||
           kind == VOICE_LINK_TELEMETRY;
}

bool voice_link_encode(uint8_t *out, size_t out_size, const voice_link_packet_t *packet, size_t *encoded_size) {
    if (out == NULL || packet == NULL || !is_known_kind(packet->header.kind) || packet->header.flags != 0 ||
        packet->header.payload_size > VOICE_LINK_MAX_PAYLOAD ||
        out_size < VOICE_LINK_HEADER_SIZE + packet->header.payload_size ||
        (packet->header.payload_size > 0 && packet->payload == NULL)) {
        return false;
    }

    out[0] = VOICE_LINK_VERSION;
    out[1] = packet->header.kind;
    write_u16_le(&out[2], packet->header.sequence);
    write_u32_le(&out[4], packet->header.timestamp_ms);
    write_u16_le(&out[8], packet->header.payload_size);
    out[10] = packet->header.mouth_level;
    out[11] = packet->header.flags;
    if (packet->header.payload_size > 0) {
        memcpy(&out[VOICE_LINK_HEADER_SIZE], packet->payload, packet->header.payload_size);
    }
    if (encoded_size != NULL) {
        *encoded_size = VOICE_LINK_HEADER_SIZE + packet->header.payload_size;
    }
    return true;
}

bool voice_link_decode(const uint8_t *data, size_t data_size, voice_link_packet_t *packet) {
    if (data == NULL || packet == NULL || data_size < VOICE_LINK_HEADER_SIZE || data[0] != VOICE_LINK_VERSION) {
        return false;
    }

    const uint16_t payload_size = read_u16_le(&data[8]);
    if (payload_size > VOICE_LINK_MAX_PAYLOAD || data[11] != 0 || !is_known_kind((voice_link_kind_t)data[1]) ||
        data_size != VOICE_LINK_HEADER_SIZE + payload_size) {
        return false;
    }

    packet->header.version = data[0];
    packet->header.kind = (voice_link_kind_t)data[1];
    packet->header.sequence = read_u16_le(&data[2]);
    packet->header.timestamp_ms = read_u32_le(&data[4]);
    packet->header.payload_size = payload_size;
    packet->header.mouth_level = data[10];
    packet->header.flags = data[11];
    packet->payload = &data[VOICE_LINK_HEADER_SIZE];
    return true;
}
