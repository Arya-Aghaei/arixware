#pragma once
#include <stdbool.h>
#include <stddef.h>

#define NET_TIMEZONE_NAME_MAX 63
/* Immutable POSIX rule for an IANA name, or NULL for unknown names. */
const char *net_timezone_rule(const char *name);
/* Load the last detected timezone; UTC until first successful detection. */
void net_timezone_init(void);
/* Validate, persist and apply a detected IANA timezone. */
bool net_timezone_set(const char *name);
/* Read the persisted IANA name; leaves out empty if none is available. */
bool net_timezone_get(char *out, size_t size);
