#include "audio.h"
#include <string.h>
#include <stdatomic.h>
#include "esp_log.h"
#include "esp_mmap_assets.h"
#include "esp_codec_dev.h"
#include "bsp/esp-bsp.h"
#include "mmap_generate_sounds.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define AUDIO_TASK_STACK  4096
#define AUDIO_TASK_PRIO   5
#define AUDIO_QUEUE_LEN   4
#define DEFAULT_VOLUME    70
#define PLAY_CHUNK_BYTES  4096 /* long clips (alarms) are written in slices so they can be cut short */
#define WAV_SEARCH_LIMIT  128 /* RIFF header is tiny; no need to scan the whole buffer for "data" */

static const char *TAG = "audio";

static mmap_assets_handle_t s_assets;
static esp_codec_dev_handle_t s_speaker;
static QueueHandle_t s_queue;
static atomic_bool s_muted;
static atomic_int s_volume = DEFAULT_VOLUME;
static atomic_int s_pending;   /* queued or playing clips */
static atomic_bool s_abort;    /* cut the clip being played */

/* Locates the PCM payload inside a RIFF/WAVE buffer by scanning for the "data" chunk,
 * since chunk order/size (e.g. extra "LIST" metadata) isn't guaranteed fixed-offset. */
static const uint8_t *locate_pcm(const uint8_t *buf, int len, size_t *pcm_len)
{
    int scan_end = (len < WAV_SEARCH_LIMIT) ? len : WAV_SEARCH_LIMIT;
    for (int i = 12; i + 8 <= scan_end; i++) {
        if (memcmp(&buf[i], "data", 4) == 0) {
            uint32_t data_len = (uint32_t)buf[i + 4] | ((uint32_t)buf[i + 5] << 8) |
                                ((uint32_t)buf[i + 6] << 16) | ((uint32_t)buf[i + 7] << 24);
            size_t data_start = i + 8;
            if (data_start > (size_t)len) {
                return NULL;
            }
            size_t avail = (size_t)len - data_start;
            *pcm_len = (data_len < avail) ? data_len : avail;
            return buf + data_start;
        }
    }
    return NULL;
}

static void play_index_blocking(int index)
{
    if (!s_assets || !s_speaker) {
        return;
    }
    const uint8_t *raw = mmap_assets_get_mem(s_assets, index);
    int raw_len = mmap_assets_get_size(s_assets, index);
    if (!raw || raw_len <= 0) {
        ESP_LOGW(TAG, "Sound index %d missing from assets", index);
        return;
    }

    size_t pcm_len = 0;
    const uint8_t *pcm = locate_pcm(raw, raw_len, &pcm_len);
    if (!pcm || pcm_len == 0) {
        ESP_LOGW(TAG, "Sound index %d: could not find WAV data chunk", index);
        return;
    }

    /* Codec is opened once in audio_init() and kept open: opening/closing per clip
     * re-runs the ES8311's clock-divider config and PA power/mute ramp every time,
     * which is what caused the audible delay, fade-in and tail-end breakup. */
    for (size_t done = 0; done < pcm_len && !atomic_load(&s_abort); done += PLAY_CHUNK_BYTES) {
        size_t n = pcm_len - done < PLAY_CHUNK_BYTES ? pcm_len - done : PLAY_CHUNK_BYTES;
        if (esp_codec_dev_write(s_speaker, (void *)(pcm + done), (int)n) != ESP_CODEC_DEV_OK) {
            ESP_LOGW(TAG, "Sound index %d: write failed at byte %u", index, (unsigned)done);
            break;
        }
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    int index;
    while (1) {
        if (xQueueReceive(s_queue, &index, portMAX_DELAY) == pdTRUE) {
            if (!atomic_load(&s_muted) && audio_get_volume() > 0) {
                atomic_store(&s_abort, false);
                play_index_blocking(index);
            }
            atomic_fetch_sub(&s_pending, 1);
        }
    }
}

void audio_init(void)
{
    const mmap_assets_config_t cfg = {
        .partition_label = "sounds",
        .max_files = MMAP_SOUNDS_FILES,
        .checksum = MMAP_SOUNDS_CHECKSUM,
        .flags = { .mmap_enable = 1 },
    };
    if (mmap_assets_new(&cfg, &s_assets) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open sound assets; audio disabled");
        return;
    }

    bsp_audio_init(NULL);
    s_speaker = bsp_audio_codec_speaker_init();
    if (!s_speaker) {
        ESP_LOGW(TAG, "Speaker codec init failed; audio disabled");
        return;
    }
    esp_codec_dev_set_out_vol(s_speaker, audio_get_volume());

    /* All sound assets are mono 16-bit 16kHz PCM (verified when authored). Opened once and
     * left open for the device's lifetime — see note in play_index_blocking(). */
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .sample_rate = 16000,
    };
    if (esp_codec_dev_open(s_speaker, &fs) != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Speaker codec open failed; audio disabled");
        s_speaker = NULL;
        return;
    }

    s_queue = xQueueCreate(AUDIO_QUEUE_LEN, sizeof(int));
    xTaskCreate(audio_task, "audio", AUDIO_TASK_STACK, NULL, AUDIO_TASK_PRIO, NULL);
}

void audio_play_index(int index)
{
    if (!s_queue || atomic_load(&s_muted) || audio_get_volume() == 0) {
        return;
    }
    atomic_fetch_add(&s_pending, 1);
    if (xQueueSend(s_queue, &index, 0) != pdTRUE) {
        atomic_fetch_sub(&s_pending, 1);
    }
}

bool audio_is_playing(void)
{
    return atomic_load(&s_pending) > 0;
}

void audio_stop(void)
{
    if (!s_queue) {
        return;
    }
    int index;
    while (xQueueReceive(s_queue, &index, 0) == pdTRUE) {
        atomic_fetch_sub(&s_pending, 1);
    }
    atomic_store(&s_abort, true);
}

void audio_set_volume(int volume)
{
    volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    atomic_store(&s_volume, volume);
    if (s_speaker) {
        esp_codec_dev_set_out_vol(s_speaker, atomic_load(&s_muted) ? 0 : volume);
    }
}

int audio_get_volume(void)
{
    return atomic_load(&s_volume);
}

void audio_set_muted(bool muted)
{
    atomic_store(&s_muted, muted);
    if (muted) {
        audio_stop();
    }
    if (s_speaker) {
        esp_codec_dev_set_out_vol(s_speaker, muted ? 0 : audio_get_volume());
    }
}
