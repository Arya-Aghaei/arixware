#include "net_timezone.h"
#include "nvs.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "timezone_rules.inc"

const char *net_timezone_rule(const char *name)
{
    if (!name || !name[0] || strlen(name) > NET_TIMEZONE_NAME_MAX) return NULL;
    size_t lo = 0, hi = sizeof(TIMEZONE_RULES)/sizeof(TIMEZONE_RULES[0]);
    while (lo < hi) {
        size_t mid = lo + (hi-lo)/2;
        int order = strcmp(name, TIMEZONE_RULES[mid].name);
        if (!order) return TIMEZONE_RULES[mid].rule;
        if (order < 0) hi = mid; else lo = mid+1;
    }
    return NULL;
}

bool net_timezone_get(char *out, size_t size)
{
    if (!out || !size) return false;
    out[0] = 0;
    nvs_handle_t handle;
    if (nvs_open("device_timezone", NVS_READONLY, &handle) != ESP_OK) return false;
    esp_err_t error = nvs_get_str(handle, "iana_name", out, &size);
    nvs_close(handle);
    if (error != ESP_OK || !net_timezone_rule(out)) { out[0] = 0; return false; }
    return true;
}

void net_timezone_init(void)
{
    char name[NET_TIMEZONE_NAME_MAX+1];
    const char *rule = net_timezone_get(name, sizeof(name)) ? net_timezone_rule(name) : "UTC0";
    setenv("TZ", rule, 1);
    tzset();
}

bool net_timezone_set(const char *name)
{
    const char *rule = net_timezone_rule(name);
    if (!rule) return false;
    char previous[NET_TIMEZONE_NAME_MAX+1];
    if (!net_timezone_get(previous, sizeof(previous)) || strcmp(previous, name)) {
        nvs_handle_t handle;
        if (nvs_open("device_timezone", NVS_READWRITE, &handle) != ESP_OK) return false;
        esp_err_t error = nvs_set_str(handle, "iana_name", name);
        if (error == ESP_OK) error = nvs_commit(handle);
        nvs_close(handle);
        if (error != ESP_OK) return false;
    }
    if (setenv("TZ", rule, 1) != 0) return false;
    tzset();
    return true;
}
