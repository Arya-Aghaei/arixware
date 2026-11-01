/* Exercise the production scheduler with LVGL's virtual clock. */
#include "../../components/eyes_renderer/expressive_eyes.c"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    lv_init();
    reset_idle_personality(true);
    s_expression = EXPRESSION_NEUTRAL;
    int nap_frames = 0;
    float yawn = 0, dream = 0, flutter = 0;
    /* Ten hours of virtual time checks the distribution, not a fixed bedtime. */
    const int simulation_ms = 36000000;
    for (int elapsed = 0; elapsed < simulation_ms; elapsed += 16)
    {
        float previous = s_idle_lid;
        lv_tick_inc(16);
        assert(step_idle_personality(0.016f));
        step_gaze(0.016f);
        step_blink(16);
        assert(s_expression == EXPRESSION_NEUTRAL);
        assert(fabsf(s_idle_lid - previous) < 0.04f);
        assert(isfinite(s_gaze_x) && fabsf(s_gaze_x) < 9.0f);
        assert(s_idle_lid >= 0.03f && s_idle_lid <= 1.0f);
        nap_frames += s_idle_mood == IDLE_NAPPING;
        yawn = fmaxf(yawn, s_idle_yawn);
        dream = fmaxf(dream, s_idle_dream);
        flutter = fmaxf(flutter, s_idle_flutter);
    }
    assert(nap_frames > 0 && nap_frames < simulation_ms / 16 / 4);
    assert(yawn > 0.8f && dream > 0.8f && flutter > 0.04f);
    printf("Nap fraction: %.1f%%; yawn/dream/flutter observed\n", 100.0f * nap_frames / (simulation_ms / 16));

    /* One dream must stay visible through the longest nap, across GIF loops
     * and eyelid twitches, without restarting its playback clock. */
    reset_idle_personality(true);
    s_idle_since = lv_tick_get() - 300001u;
    s_idle_mood = IDLE_DROWSY;
    s_idle_mood_until = lv_tick_get();
    step_idle_personality(0.016f);
    assert(s_idle_mood == IDLE_NAPPING);
    uint32_t nap_start = lv_tick_get();
    s_idle_mood_until = nap_start + 50000u;
    for (int elapsed = 0; elapsed < 49984; elapsed += 16)
    {
        lv_tick_inc(16);
        step_idle_personality(0.016f);
        if (elapsed > 2000) assert(s_idle_dream > 0.99f);
        assert(s_idle_dream_since == nap_start);
    }
    lv_tick_inc(32);
    step_idle_personality(0.016f);
    assert(s_idle_mood == IDLE_WAKING);
    for (int i = 0; i < 125; i++) { lv_tick_inc(16); step_idle_personality(0.016f); }
    assert(s_idle_dream == 0.0f);

    /* Even unlucky random draws cannot postpone sleep indefinitely. */
    for (int seed = 1; seed <= 20; seed++)
    {
        srand(seed);
        reset_idle_personality(true);
        bool slept = false;
        for (int elapsed = 0; elapsed < 480000; elapsed += 100)
        {
            lv_tick_inc(100);
            step_idle_personality(0.05f);
            if (s_idle_mood == IDLE_NAPPING) { slept = true; break; }
        }
        assert(slept);
    }
    /* A finished nap releases sleep pressure; interaction also restarts the wait. */
    lv_tick_inc(310000);
    s_idle_mood = IDLE_NAPPING;
    s_idle_mood_until = lv_tick_get();
    step_idle_personality(0.016f);
    assert(s_idle_mood == IDLE_WAKING && s_idle_since == lv_tick_get());

    /* A glance follows through on its original side before returning home. */
    reset_idle_personality(true);
    step_idle_personality(0.016f);
    float direction = s_gaze_tx;
    lv_tick_inc(1400);
    step_idle_personality(0.016f);
    assert(direction * s_gaze_tx > 0 && fabsf(s_gaze_tx) == 8.0f);
    lv_tick_inc(2700);
    step_idle_personality(0.016f);
    assert(s_gaze_tx == 0);

    /* External reactions own the expression until explicitly released. */
    s_wrapper = (lv_obj_t *)1;
    expressive_eyes_set_expression(EYES_EXPRESSION_ANGRY);
    lv_tick_inc(120000);
    assert(!step_idle_personality(0.016f));
    assert(s_expression == EYES_EXPRESSION_ANGRY);
    expressive_eyes_set_expression(EYES_EXPRESSION_DEFAULT);
    assert(s_idle_owned && lv_tick_get() == s_idle_since);
    s_idle_mood = IDLE_NAPPING;
    s_idle_mood_until = lv_tick_get() + 30000;
    step_idle_personality(0.016f);
    expressive_eyes_on_pickup();
    assert(!s_idle_owned && s_expression == EXPRESSION_SURPRISED);
    lv_tick_inc(SURPRISED_DURATION_MS + 1);
    lv_timer_t *timer = lv_timer_create(idle_timer_cb, 1000, NULL);
    idle_timer_cb(timer);
    lv_timer_del(timer);
    assert(s_idle_owned && s_expression == EXPRESSION_NEUTRAL);
    puts("PASS: occasional naps, yawns, dreams, flutter, continuous motion and reaction priority");
    return 0;
}
