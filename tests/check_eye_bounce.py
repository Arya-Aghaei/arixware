from pathlib import Path
import re, subprocess, tempfile
# Compile the production physics in a host harness without ESP-IDF or LVGL.
source = (Path(__file__).resolve().parents[1] /
          'components/eyes_renderer/expressive_eyes.c').read_text()
constants = '\n'.join(re.findall(r'^#define (?:EYE_[WHG]\w*|EYE_MIN_GAP|BOUNCE_\w+|DISPLAY_SAFE_RADIUS)\s+[^\n]+', source, re.M))
physics_start = source.index('static void keep_eye_inside_display(')
physics_end = source.index('\nstatic ', source.index('static void integrate_bounce_step('))
functions = source[physics_start:physics_end]
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#define LV_CLAMP(lo,v,hi) fminf(fmaxf((lo),(v)),(hi))
typedef struct { int32_t base_x; float bounce_x,bounce_y,velocity_x,velocity_y; } eye_render_t;
static int16_t s_gaze_x, s_gaze_y;
static bool s_shake_active;
static float s_shake_force_x,s_shake_force_y;
'''
# base_x is derived from the *current* EYE_W/EYE_GAP (matches make_eye_canvas's x_offset) so this
# harness can't silently go stale against the production geometry the way a hardcoded value would.
eyes_init = 'static eye_render_t s_eyes[2] = {{.base_x=-(EYE_W/2+EYE_GAP/2)}, {.base_x=(EYE_W/2+EYE_GAP/2)}};\n'
checks = r'''
static void check(void) {
    float centers[2];
    for(int i=0;i<2;i++) {
        eye_render_t *e=&s_eyes[i];
        float x=e->base_x+s_gaze_x+e->bounce_x;
        float y=s_gaze_y+e->bounce_y;
        centers[i]=x;
        assert(isfinite(x) && isfinite(y));
        float ex=fabsf(x)+EYE_W/2.0f, ey=fabsf(y)+EYE_H/2.0f;
        assert(ex*ex+ey*ey <= DISPLAY_SAFE_RADIUS*DISPLAY_SAFE_RADIUS+0.1f);
        assert((i==0 ? -x:x) >= EYE_W/2.0f+3.99f);
    }
    assert(centers[1]-centers[0] >= EYE_W+7.99f);
}
int main(void) {
    /* Reproduce a diagonal overlap missed by the old width-only circle test. */
    assert(hypotf(100,100) > EYE_W*.75f+8);
    s_eyes[0].bounce_x=37; s_eyes[0].bounce_y=-45;
    s_eyes[1].bounce_x=-37; s_eyes[1].bounce_y=45;
    for(int i=0;i<2;i++) keep_eye_inside_display(&s_eyes[i]);
    check();
    /* Exercise maximum forces, gaze updates, frame stalls, and repeated shakes. */
    for(int n=0;n<100000;n++) {
        s_shake_active=n%1500<1000;
        s_shake_force_x=s_shake_active ? 12000*sinf(n*.013f):0;
        s_shake_force_y=s_shake_active ? 12000*cosf(n*.019f):0;
        s_gaze_x=(int16_t)(12*sinf(n*.007f));
        s_gaze_y=(int16_t)(8*cosf(n*.011f));
        for(int i=0;i<2;i++) keep_eye_inside_display(&s_eyes[i]);
        check();
        for(int step=0;step<5;step++) { integrate_bounce_step(.008f); check(); }
    }
    s_shake_active=false; s_shake_force_x=s_shake_force_y=0;
    s_gaze_x=s_gaze_y=0;
    for(int n=0;n<2000;n++) {integrate_bounce_step(.008f);check();}
    for(int i=0;i<2;i++) {
        assert(fabsf(s_eyes[i].bounce_x)<.01f && fabsf(s_eyes[i].bounce_y)<.01f);
        assert(fabsf(s_eyes[i].velocity_x)<.01f && fabsf(s_eyes[i].velocity_y)<.01f);
    }
    puts("PASS: diagonal overlap, 500000 physics steps with gaze/force changes, and return to rest");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c=Path(tmp)/'bounce.c'; exe=Path(tmp)/'bounce'
    c.write_text(harness+'\n'+constants+'\n'+eyes_init+functions+checks)
    subprocess.run(['cc','-Wall','-Wextra','-Werror',str(c),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
