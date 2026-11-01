#include "net_dns_packet.h"
#include <stdbool.h>
#include <string.h>

size_t net_dns_reply(uint8_t *packet, size_t length, size_t capacity)
{
    if (length < 17 || length > capacity || packet[4] != 0 || packet[5] != 1 || (packet[2] & 0xf8)) return 0;
    size_t end = 12;
    while (end < length && packet[end] && packet[end] <= 63 && end + packet[end] + 1 < length) end += packet[end] + 1;
    if (end + 5 > length || packet[end] != 0 || end - 12 > 253) return 0;
    end++;
    bool ipv4 = packet[end] == 0 && packet[end + 1] == 1 && packet[end + 2] == 0 && packet[end + 3] == 1;
    end += 4;
    if (ipv4 && end + 16 > capacity) return 0;
    packet[2] = 0x84; packet[3] = 0;
    packet[6] = 0; packet[7] = ipv4 ? 1 : 0;
    memset(packet + 8, 0, 4);
    if (ipv4) {
        const uint8_t answer[] = {0xc0,0x0c,0,1,0,1,0,0,0,0,0,4,192,168,4,1};
        memcpy(packet + end, answer, sizeof(answer));
        end += sizeof(answer);
    }
    return end;
}
