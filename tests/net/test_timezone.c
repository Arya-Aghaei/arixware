#include "net_timezone.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char stored[NET_TIMEZONE_NAME_MAX+1];
static bool exists;
static int commits, opens, closes;
static esp_err_t read_error, write_error, commit_error;
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(!strcmp(name, "device_timezone"));
    if (!exists && mode == NVS_READONLY) return ESP_ERR_NVS_NOT_FOUND;
    *handle = 1; opens++; return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out, size_t *size)
{
    assert(handle == 1 && !strcmp(key, "iana_name"));
    if (read_error) return read_error;
    if (*size < strlen(stored)+1) return ESP_ERR_NVS_INVALID_LENGTH;
    strcpy(out, stored); return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value)
{
    assert(handle == 1 && !strcmp(key, "iana_name"));
    if (write_error) return write_error;
    strcpy(stored, value); exists = true; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) { assert(handle == 1); commits++; return commit_error; }
void nvs_close(nvs_handle_t handle) { assert(handle == 1); closes++; }
static void check(time_t utc, int hour, int minute)
{
    struct tm local;
    assert(localtime_r(&utc, &local));
    assert(local.tm_hour == hour && local.tm_min == minute);
}
int main(void)
{
    char name[NET_TIMEZONE_NAME_MAX+1];
    net_timezone_init();
    assert(!strcmp(getenv("TZ"), "UTC0"));
    assert(!net_timezone_get(name, sizeof(name)) && !name[0]);
    assert(!net_timezone_set(NULL) && !net_timezone_set(""));
    assert(!net_timezone_set("Not/A_Zone") && !net_timezone_set("Europe/Berlin\n"));
    assert(net_timezone_set("Europe/Berlin"));
    assert(net_timezone_get(name, sizeof(name)) && !strcmp(name, "Europe/Berlin"));
    check(1768478400, 13, 0); /* January, UTC+1 */
    check(1784116800, 14, 0); /* July, UTC+2 */
    check(1774745940, 1, 59); check(1774746000, 3, 0); /* Spring gap */
    check(1792889940, 2, 59); check(1792890000, 2, 0); /* Autumn overlap */
    int previous = commits;
    assert(net_timezone_set("Europe/Berlin") && commits == previous); /* Avoid flash wear. */
    setenv("TZ", "UTC0", 1); tzset();
    net_timezone_init(); check(1768478400, 13, 0); /* Offline reboot restores cached zone. */
    assert(net_timezone_set("America/New_York"));
    check(1768478400, 7, 0); check(1784116800, 8, 0);
    assert(net_timezone_set("Asia/Kolkata")); check(1768478400, 17, 30);
    assert(net_timezone_set("Asia/Tehran")); check(1768478400, 15, 30);
    assert(net_timezone_set("Australia/Sydney"));
    check(1768478400, 23, 0); check(1784116800, 22, 0);
    write_error = ESP_FAIL;
    assert(!net_timezone_set("Europe/Berlin")); check(1768478400, 23, 0);
    write_error = ESP_OK; commit_error = ESP_FAIL;
    assert(!net_timezone_set("Europe/Berlin")); check(1768478400, 23, 0);
    commit_error = ESP_OK; read_error = ESP_FAIL;
    assert(!net_timezone_get(name, sizeof(name)) && !name[0]);
    read_error = ESP_OK;
    strcpy(stored, "Unknown/Zone");
    net_timezone_init(); check(1768478400, 12, 0);
    assert(opens == closes);
    puts("PASS: detected zones, DST, fractional offsets, offline cache, validation and storage errors");
}
