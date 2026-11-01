#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define NET_PROFILE_OWNER_MAX 64 /* UTF-8 bytes, excluding the terminator. */

/** Trim surrounding ASCII whitespace and validate a printable, nonempty owner name. */
bool net_profile_normalize_owner(const char *name, char out[NET_PROFILE_OWNER_MAX + 1]);
/** Read the saved name, independently of Wi-Fi initialization. NVS must be initialized.
 * Missing names and read failures leave out empty. */
esp_err_t net_profile_get_owner(char *out, size_t size);
/** Persist a validated name. Call only after a setup connection succeeds. */
esp_err_t net_profile_save_owner(const char *name);
