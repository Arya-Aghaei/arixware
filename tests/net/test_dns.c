#include "net_dns_packet.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void)
{
    const uint8_t query[] = {0x12,0x34,1,0,0,1,0,0,0,0,0,0,3,'w','w','w',4,'t','e','s','t',0,0,1,0,1};
    uint8_t packet[512];
    memcpy(packet, query, sizeof(query));
    size_t n = net_dns_reply(packet, sizeof(query), sizeof(packet));
    assert(n == sizeof(query) + 16 && packet[0] == 0x12 && packet[1] == 0x34 && packet[7] == 1);
    assert(!memcmp(packet + n - 4, (uint8_t[]){192,168,4,1}, 4));
    for (size_t len = 0; len < sizeof(query); len++) {
        memcpy(packet, query, sizeof(query));
        assert(net_dns_reply(packet, len, sizeof(packet)) == 0);
    }
    memcpy(packet, query, sizeof(query));
    packet[sizeof(query) - 3] = 28; /* AAAA has no IPv4 answer. */
    n = net_dns_reply(packet, sizeof(query), sizeof(packet));
    assert(n == sizeof(query) && packet[7] == 0);
    memcpy(packet, query, sizeof(query));
    assert(net_dns_reply(packet, sizeof(query), sizeof(query)) == 0);
    memcpy(packet, query, sizeof(query));
    packet[12] = 0xc0; /* Compressed question names are rejected. */
    assert(net_dns_reply(packet, sizeof(query), sizeof(packet)) == 0);
    unsigned state = 7;
    for (int trial = 0; trial < 30000; trial++) {
        for (size_t i = 0; i < sizeof(packet); i++) { state = state * 1664525U + 1013904223U; packet[i] = state >> 24; }
        size_t len = state % sizeof(packet);
        assert(net_dns_reply(packet, len, sizeof(packet)) <= sizeof(packet));
    }
    puts("Captive DNS: A/AAAA, malformed/truncated packets, bounds and 30,000 fuzz cases passed");
}
