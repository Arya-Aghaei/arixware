#include "eyes_panel.h"
#include "lvgl.h"
#include "esp_lv_adapter.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "eyes_panel";
static lv_display_t *s_disp;
static int s_owners;

bool eyes_panel_begin(void)
{
    if (s_owners > 0) {
        s_owners++;
        return true;
    }
    lv_display_t *disp = lv_display_get_default();
    if (!disp) {
        return false;
    }
    esp_err_t err = esp_lv_adapter_set_dummy_draw(disp, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "enable direct draw failed: %s", esp_err_to_name(err));
        return false;
    }
    s_disp = disp;
    s_owners = 1;
    return true;
}

void eyes_panel_end(void)
{
    if (!s_disp || --s_owners > 0) {
        return;
    }
    /* The adapter hands the panel back to LVGL and schedules a full redraw. */
    esp_err_t err = esp_lv_adapter_set_dummy_draw(s_disp, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "disable direct draw failed: %s", esp_err_to_name(err));
    }
    s_disp = NULL;
}

void *eyes_panel_alloc_dma(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
}

void eyes_panel_free_dma(void *buf)
{
    heap_caps_free(buf);
}

bool eyes_panel_blit(int x0, int y0, int x1, int y1, const void *buf, bool wait)
{
    if (!s_disp) {
        return false;
    }
    esp_err_t err = esp_lv_adapter_dummy_draw_blit(s_disp, x0, y0, x1, y1, buf, wait);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "blit (%d,%d)-(%d,%d) failed: %s", x0, y0, x1, y1, esp_err_to_name(err));
        return false;
    }
    return true;
}

/* Helper for eyes_panel_run_async(): unpinned at the caller's priority, so it runs on the core the
 * caller isn't busy on. Created once and kept. */
static TaskHandle_t s_worker;
static SemaphoreHandle_t s_worker_done;
static void (*volatile s_worker_fn)(void *arg);
static void *volatile s_worker_arg;
static bool s_worker_busy;

static void worker_task(void *unused)
{
    (void)unused;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        s_worker_fn(s_worker_arg);
        xSemaphoreGive(s_worker_done);
    }
}

void eyes_panel_run_async(void (*fn)(void *arg), void *arg)
{
    eyes_panel_join();
    if (!s_worker) {
        s_worker_done = xSemaphoreCreateBinary();
        if (!s_worker_done ||
            xTaskCreatePinnedToCore(worker_task, "eyes_raster", 6144, NULL, uxTaskPriorityGet(NULL), &s_worker,
                                    tskNO_AFFINITY) != pdPASS) {
            ESP_LOGW(TAG, "raster helper unavailable; rasterizing inline");
            s_worker = NULL;
            fn(arg);
            return;
        }
    }
    s_worker_fn = fn;
    s_worker_arg = arg;
    s_worker_busy = true;
    xTaskNotifyGive(s_worker);
}

void eyes_panel_join(void)
{
    if (s_worker_busy) {
        xSemaphoreTake(s_worker_done, portMAX_DELAY);
        s_worker_busy = false;
    }
}
