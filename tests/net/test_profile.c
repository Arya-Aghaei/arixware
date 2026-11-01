#include "net_profile.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char stored[NET_PROFILE_OWNER_MAX + 1];
static bool exists;
static int opens, closes, commits;
static esp_err_t open_error, write_error, commit_error;
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(!strcmp(name, "device_profile"));
    if (open_error) return open_error;
    if (!exists && mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
    *handle = 1; opens++; return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *value, size_t *size)
{
    assert(h == 1 && !strcmp(key, "owner_name"));
    if (!exists) return ESP_ERR_NVS_NOT_FOUND;
    if (*size < strlen(stored) + 1) return ESP_ERR_NVS_INVALID_LENGTH;
    strcpy(value, stored); return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value)
{
    assert(h == 1 && !strcmp(key, "owner_name"));
    if (write_error) return write_error;
    strcpy(stored, value); exists = true; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { assert(h == 1); commits++; return commit_error; }
void nvs_close(nvs_handle_t h) { assert(h == 1); closes++; }

int main(void)
{
    char owner[NET_PROFILE_OWNER_MAX + 1] = "stale";
    assert(net_profile_get_owner(owner, sizeof(owner)) == ESP_ERR_NVS_NOT_FOUND && !owner[0]);
    assert(net_profile_save_owner(" \tArya \n") == ESP_OK);
    assert(commits == 1);
    /* No initialization/cache is needed: a new caller reads directly from NVS. */
    assert(net_profile_get_owner(owner, sizeof(owner)) == ESP_OK && !strcmp(owner, "Arya"));
    assert(net_profile_save_owner("Zoë 李") == ESP_OK);
    assert(net_profile_get_owner(owner, sizeof(owner)) == ESP_OK && !strcmp(owner, "Zoë 李"));
    assert(net_profile_save_owner("<script>alert(1)</script>") == ESP_OK); /* Stored as text. */
    char boundary[NET_PROFILE_OWNER_MAX + 2]; memset(boundary, 'A', sizeof(boundary));
    boundary[NET_PROFILE_OWNER_MAX] = '\0';
    assert(net_profile_save_owner(boundary) == ESP_OK);
    boundary[NET_PROFILE_OWNER_MAX] = 'A'; boundary[NET_PROFILE_OWNER_MAX + 1] = '\0';
    int previous_opens = opens;
    assert(net_profile_save_owner(boundary) == ESP_ERR_INVALID_ARG);
    assert(net_profile_save_owner("") == ESP_ERR_INVALID_ARG);
    assert(net_profile_save_owner("  \t\n") == ESP_ERR_INVALID_ARG);
    assert(net_profile_save_owner("First\nLast") == ESP_ERR_INVALID_ARG);
    assert(net_profile_save_owner(NULL) == ESP_ERR_INVALID_ARG);
    assert(opens == previous_opens);
    write_error = ESP_FAIL;
    int previous_commits = commits;
    assert(net_profile_save_owner("New owner") == ESP_FAIL && commits == previous_commits);
    write_error = ESP_OK; commit_error = ESP_FAIL;
    assert(net_profile_save_owner("New owner") == ESP_FAIL);
    commit_error = ESP_OK; open_error = ESP_FAIL;
    strcpy(owner, "stale");
    assert(net_profile_get_owner(owner, sizeof(owner)) == ESP_FAIL && !owner[0]);
    assert(net_profile_save_owner("New owner") == ESP_FAIL);
    open_error = ESP_OK;
    owner[0] = 'X';
    assert(net_profile_get_owner(owner, 1) == ESP_ERR_NVS_INVALID_LENGTH && !owner[0]);
    assert(opens == closes);
    puts("Device profile: trimming, UTF-8 names, byte limits, NVS reads/writes and failure handling passed");
}
