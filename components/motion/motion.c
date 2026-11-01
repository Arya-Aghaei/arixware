#include "motion.h"
#include <math.h>
#include "qmi8658.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "lvgl.h"

#define POLL_PERIOD_MS          30
#define PICKUP_COOLDOWN_MS      900
#define MAG_DEVIATION_THRESHOLD 2.5f   /* m/s^2 away from 1g: sudden movement/grab */
#define TILT_DOT_THRESHOLD      0.85f  /* cos(~32deg): loose enough to ignore sensor jitter */
#define TRIGGER_DEBOUNCE_COUNT  2      /* consecutive readings required before firing, filters single-sample noise */
#define BASELINE_LPF            0.05f
#define SHAKE_BASELINE_LPF      0.08f
#define SHAKE_ACCEL_THRESHOLD   1.8f
#define SHAKE_REVERSAL_WINDOW_MS 500
#define SHAKE_REVERSALS_REQUIRED 2
#define SHAKE_ACTIVE_TIMEOUT_MS  180
#define ONE_G_MPS2              9.807f
#define MAG_SANITY_MIN          3.0f   /* implausible readings (bad I2C transfer) are ignored outright */
#define MAG_SANITY_MAX          25.0f

static const char *TAG = "motion";

static qmi8658_dev_t s_dev;
static bool s_ready;
static void (*s_pickup_cb)(void);
static void (*s_shake_cb)(float accel_x, float accel_y, bool active);
static float s_base_x, s_base_y, s_base_z = ONE_G_MPS2;
static bool s_base_init;
static float s_shake_base_x, s_shake_base_y, s_shake_base_z;
static float s_prev_shake_x, s_prev_shake_y, s_prev_shake_z;
static float s_prev_shake_mag;
static uint32_t s_last_shake_reversal;
static uint32_t s_last_shake_energy;
static uint8_t s_shake_reversals;
static bool s_shake_active;
static bool s_was_shake_active;
static uint32_t s_cooldown_until;
static uint8_t s_trigger_streak;

static void update_shake_feedback(float x, float y, float z)
{
    float shake_x = x - s_shake_base_x;
    float shake_y = y - s_shake_base_y;
    float shake_z = z - s_shake_base_z;
    float shake_mag = sqrtf(shake_x * shake_x + shake_y * shake_y + shake_z * shake_z);
    uint32_t now = lv_tick_get();

    if (shake_mag >= SHAKE_ACCEL_THRESHOLD && s_prev_shake_mag >= SHAKE_ACCEL_THRESHOLD) {
        float dot = shake_x * s_prev_shake_x + shake_y * s_prev_shake_y + shake_z * s_prev_shake_z;
        if (dot < -0.25f * shake_mag * s_prev_shake_mag) {
            if (s_last_shake_reversal == 0 || now - s_last_shake_reversal > SHAKE_REVERSAL_WINDOW_MS) {
                s_shake_reversals = 1;
            } else {
                s_shake_reversals++;
            }
            s_last_shake_reversal = now;
            if (s_shake_reversals >= SHAKE_REVERSALS_REQUIRED) {
                s_shake_active = true;
            }
        }
    }

    if (shake_mag >= SHAKE_ACCEL_THRESHOLD) {
        s_last_shake_energy = now;
    }
    s_prev_shake_x = shake_x;
    s_prev_shake_y = shake_y;
    s_prev_shake_z = shake_z;
    s_prev_shake_mag = shake_mag;

    if (s_shake_active && now - s_last_shake_energy > SHAKE_ACTIVE_TIMEOUT_MS) {
        s_shake_active = false;
        s_shake_reversals = 0;
        s_last_shake_reversal = 0;
        if (s_shake_cb) {
            s_shake_cb(0.0f, 0.0f, false);
        }
    } else if (s_shake_active && s_shake_cb) {
        /* Map depth-axis motion onto both display axes so any shaking direction is visible. */
        s_shake_cb(shake_x + shake_z * 0.7f, shake_y + shake_z * 0.7f, true);
    }

    s_shake_base_x += (x - s_shake_base_x) * SHAKE_BASELINE_LPF;
    s_shake_base_y += (y - s_shake_base_y) * SHAKE_BASELINE_LPF;
    s_shake_base_z += (z - s_shake_base_z) * SHAKE_BASELINE_LPF;
}

static void poll_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_ready) {
        return;
    }

    float x, y, z;
    if (qmi8658_read_accel(&s_dev, &x, &y, &z) != ESP_OK) {
        return;
    }

    float mag = sqrtf(x * x + y * y + z * z);
    if (mag < MAG_SANITY_MIN || mag > MAG_SANITY_MAX) {
        return; /* almost certainly a corrupted I2C transfer, not real motion */
    }

    if (!s_base_init) {
        s_base_x = x;
        s_base_y = y;
        s_base_z = z;
        s_base_init = true;
        s_shake_base_x = x;
        s_shake_base_y = y;
        s_shake_base_z = z;
        return;
    }

    update_shake_feedback(x, y, z);

    if (s_shake_active) {
        /* A recognized shake gesture is not also a pickup; keep the two reactions distinct
         * and don't let the shake's own large swings decay the resting baseline. */
        s_trigger_streak = 0;
        s_was_shake_active = true;
        return;
    }
    if (s_was_shake_active) {
        /* Shake just ended: resync instead of comparing residual motion to a stale baseline. */
        s_base_x = x;
        s_base_y = y;
        s_base_z = z;
        s_was_shake_active = false;
        s_trigger_streak = 0;
        return;
    }

    bool was_in_cooldown = s_cooldown_until != 0 && lv_tick_get() < s_cooldown_until;
    if (s_cooldown_until != 0 && !was_in_cooldown) {
        /* Cooldown just ended: resync to "now" instead of comparing against a stale baseline. */
        s_base_x = x;
        s_base_y = y;
        s_base_z = z;
        s_cooldown_until = 0;
        s_trigger_streak = 0;
        return;
    }
    if (was_in_cooldown) {
        return;
    }

    float mag_dev = fabsf(mag - ONE_G_MPS2);
    float base_mag = sqrtf(s_base_x * s_base_x + s_base_y * s_base_y + s_base_z * s_base_z);
    float dot = (x * s_base_x + y * s_base_y + z * s_base_z) / (mag * base_mag + 0.0001f);
    bool triggered = (mag_dev > MAG_DEVIATION_THRESHOLD) || (dot < TILT_DOT_THRESHOLD);

    if (triggered) {
        s_trigger_streak++;
        if (s_trigger_streak >= TRIGGER_DEBOUNCE_COUNT) {
            ESP_LOGI(TAG, "Pickup detected (mag_dev=%.2f dot=%.2f)", (double)mag_dev, (double)dot);
            s_cooldown_until = lv_tick_get() + PICKUP_COOLDOWN_MS;
            s_trigger_streak = 0;
            if (s_pickup_cb) {
                s_pickup_cb();
            }
        }
    } else {
        s_trigger_streak = 0;
        /* Calm: slowly track the resting orientation as the new baseline. */
        s_base_x += (x - s_base_x) * BASELINE_LPF;
        s_base_y += (y - s_base_y) * BASELINE_LPF;
        s_base_z += (z - s_base_z) * BASELINE_LPF;
    }
}

void motion_init(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    /* This board straps the IMU at 0x6B; probing 0x6A first only logged a bogus WHO_AM_I error. */
    if (qmi8658_init(&s_dev, bus, QMI8658_ADDRESS_HIGH) != ESP_OK &&
        qmi8658_init(&s_dev, bus, QMI8658_ADDRESS_LOW) != ESP_OK) {
        ESP_LOGW(TAG, "QMI8658 not found; pickup detection disabled");
        return;
    }
    qmi8658_set_accel_range(&s_dev, QMI8658_ACCEL_RANGE_4G);
    qmi8658_set_accel_odr(&s_dev, QMI8658_ACCEL_ODR_125HZ);
    qmi8658_set_accel_unit_mps2(&s_dev, true);
    qmi8658_enable_accel(&s_dev, true);
    s_ready = true;

    lv_timer_create(poll_cb, POLL_PERIOD_MS, NULL);
}

void motion_set_pickup_cb(void (*cb)(void))
{
    s_pickup_cb = cb;
}

void motion_set_shake_cb(void (*cb)(float accel_x, float accel_y, bool active))
{
    s_shake_cb = cb;
}
