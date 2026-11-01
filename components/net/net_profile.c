#include "net_profile.h"
#include "nvs.h"
#include <ctype.h>
#include <string.h>

bool net_profile_normalize_owner(const char *name, char out[NET_PROFILE_OWNER_MAX + 1])
{
    if (!name || !out) return false;
    while (*name && isspace((unsigned char)*name)) name++;
    size_t length = strlen(name);
    while (length && isspace((unsigned char)name[length - 1])) length--;
    if (!length || length > NET_PROFILE_OWNER_MAX) return false;
    for (size_t i = 0; i < length; i++) {
        if ((unsigned char)name[i] < 0x20 || (unsigned char)name[i] == 0x7f) return false;
    }
    memcpy(out, name, length);
    out[length] = '\0';
    return true;
}

esp_err_t net_profile_get_owner(char *out, size_t size)
{
    if (!out || !size) return ESP_ERR_INVALID_ARG;
    out[0] = '\0';
    nvs_handle_t handle;
    esp_err_t err = nvs_open("device_profile", NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    err = nvs_get_str(handle, "owner_name", out, &size);
    nvs_close(handle);
    if (err != ESP_OK) out[0] = '\0';
    return err;
}

esp_err_t net_profile_save_owner(const char *name)
{
    char owner[NET_PROFILE_OWNER_MAX + 1];
    if (!net_profile_normalize_owner(name, owner)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open("device_profile", NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_str(handle, "owner_name", owner);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
