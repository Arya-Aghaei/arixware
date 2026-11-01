#pragma once
#include <stddef.h>
#include <stdint.h>

/* Returns reply length, or zero for unsupported/malformed packets. Updates in place. */
size_t net_dns_reply(uint8_t *packet, size_t length, size_t capacity);
