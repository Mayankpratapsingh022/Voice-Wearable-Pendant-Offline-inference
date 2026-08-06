#include <assert.h>
#include <string.h>

#include "voice_link.h"

int main(void) {
    const uint8_t payload[] = {0x03, 'o', 'k'};
    const voice_link_packet_t sent = {
        .header = {
            .version = VOICE_LINK_VERSION,
            .kind = VOICE_LINK_CONTROL,
            .sequence = 0x1234,
            .timestamp_ms = 0x10203040,
            .payload_size = sizeof(payload),
            .mouth_level = 0,
            .flags = 0,
        },
        .payload = payload,
    };
    uint8_t buffer[VOICE_LINK_HEADER_SIZE + sizeof(payload)];
    size_t size = 0;
    assert(voice_link_encode(buffer, sizeof(buffer), &sent, &size));
    assert(size == sizeof(buffer));
    assert(buffer[2] == 0x34 && buffer[3] == 0x12);
    assert(buffer[4] == 0x40 && buffer[7] == 0x10);

    voice_link_packet_t received;
    assert(voice_link_decode(buffer, size, &received));
    assert(received.header.kind == VOICE_LINK_CONTROL);
    assert(received.header.sequence == sent.header.sequence);
    assert(received.header.timestamp_ms == sent.header.timestamp_ms);
    assert(memcmp(received.payload, payload, sizeof(payload)) == 0);

    buffer[11] = 1;
    assert(!voice_link_decode(buffer, size, &received));
    buffer[11] = 0;
    assert(!voice_link_decode(buffer, size - 1, &received));
    return 0;
}
