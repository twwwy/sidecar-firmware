// plugins/api3_test/pagesprobe.c - the test machine for a user machine's P17..P32 and stepped knobs (0.5.0-pages,
// docs/um-pages.md; core/tests/test_um_pages.cpp). pages.json / pages2.json: P1..P16, knobs_hi P17..P32 but P31
// (P20 a list of 3), P4 a pair knob of 19 x 19 = 361 entries, P5 a stepped number of 5; max_voices 4 (poly instances).
// V=pages: a dual package (an API 2 table with an rmp3_plugin: an API 3 host calls the API 3 render); V=pages2
// (PAGES_PLAIN2): a plain API 2 plugin. Both fill reserved[RMP_P_PARAMS_HI] with params_hi. What the host gave it, as
// levels one frame each (the test reads them back from the voice's output; 12 frames a block):
//   P3 below 64: frames 0..11 = P17..P28 / 128 (as params_hi last handed them; -0.5 before any params_hi call)
//   P3 64 and up: frames 0..3 = P29..P32 / 128, 4 = P4's entry / 512 (rmp_step_entry_of(361, p[3])),
//                 5 = params_hi calls / 256, 6 = 0.5 an API 3 render with RMP3_CAP_UM_P32 in the host's caps,
//                 0.375 an API 3 render without, 0.25 the API 2 render, 7 = P5's entry / 8, 8 = P1 / 128
// Both also fill reserved[RMP_P_DRAW] (plugin-drawn screens, docs/um-pages.md; core/tests/test_um_draw.cpp): P2 1 declines
// (returns 0), P2 2 faults; else it draws, row-major (rmp_api.h): row 0 `page` pixels from x 0 and x 127; row 1 x 0
// when flags has RMP_DRAW_LOCKED, x 2 when state is not NULL, x 4 when the host's caps have RMP3_CAP_DRAW (API 3);
// row 2 track + 1 pixels; row 8 + k (k 0..31) P(k+1) as a bar of (int)p[k] pixels; row 40 P1's fraction x 128 pixels;
// rows 48..63 x lit when (x + 2 y) % 5 == 0.
// MIT, Copyright 2026 trigcondition and contributors.
#include "rmp/rmp_api3.h"

typedef struct {
  float p[16];
  float hi[16];
  int32_t hi_calls;
} pp;

static uint32_t g_caps;   /* the host's RMP3_CAP_* (0 on an API 2 host) */
static int32_t f_init(const rmp_host *h) {
#ifndef PAGES_PLAIN2
  const rmp3_host *x = rmp3_host_of(h);
  g_caps = x ? x->caps : 0u;
#else
  (void)h;
#endif
  return 0;
}
static void f_voice_init(void *st) {
  pp *s = st;
  for (int i = 0; i < 16; ++i) s->hi[i] = -64.0f;   /* frames -0.5 until params_hi */
}
static void f_note_on(void *st, float note, float vel) { (void)st; (void)note; (void)vel; }
static void f_note_off(void *st) { (void)st; }
static void f_params(void *st, const float *p) {
  pp *s = st;
  for (int i = 0; i < 16; ++i) s->p[i] = p[i];
}
static void f_params_hi(void *st, const float *p) {
  pp *s = st;
  for (int i = 0; i < 16; ++i) s->hi[i] = p[i];
  s->hi_calls++;
}
static void show(const pp *s, float *o, int32_t n, float which) {
  for (int32_t i = 0; i < n; ++i) o[i] = 0.0f;
  if (n < 12) return;
  if (s->p[2] < 64.0f) {
    for (int i = 0; i < 12; ++i) o[i] = s->hi[i] / 128.0f;
  } else {
    for (int i = 0; i < 4; ++i) o[i] = s->hi[12 + i] / 128.0f;
    o[4] = (float)rmp_step_entry_of(361u, s->p[3]) / 512.0f;
    o[5] = (float)s->hi_calls / 256.0f;
    o[6] = which;
    o[7] = (float)rmp_step_entry_of(5u, s->p[4]) / 8.0f;
    o[8] = s->p[0] / 128.0f;
  }
}
static void f_render2(void *st, float *out, int32_t n) { show(st, out, n, 0.25f); }

static void lit(uint8_t *fb, int x, int y) { fb[y * 16 + x / 8] |= (uint8_t)(0x80u >> (x % 8)); }
static void bar(uint8_t *fb, int y, int n) {
  for (int x = 0; x < n && x < RMP_DRAW_W; ++x) lit(fb, x, y);
}
static int32_t f_draw(void *st, int32_t page, uint32_t flags, int32_t track, const float *p, uint8_t *fb) {
  if (p[1] == 1.0f) return 0;
  if (p[1] == 2.0f) *(volatile int *)0 = 1;
  bar(fb, 0, page);
  lit(fb, 127, 0);
  if (flags & RMP_DRAW_LOCKED) lit(fb, 0, 1);
  if (st) lit(fb, 2, 1);
  if (g_caps & RMP3_CAP_DRAW) lit(fb, 4, 1);
  bar(fb, 2, track + 1);
  for (int k = 0; k < RMP_PARAMS + RMP_PARAMS_HI; ++k) bar(fb, 8 + k, (int)p[k]);
  bar(fb, 40, (int)((p[0] - (float)(int)p[0]) * 128.0f + 0.5f));
  for (int y = 48; y < RMP_DRAW_H; ++y)
    for (int x = 0; x < RMP_DRAW_W; ++x)
      if ((x + 2 * y) % 5 == 0) lit(fb, x, y);
  return 1;
}

#ifndef PAGES_PLAIN2
static void f_render3(void *st, const rmp3_block *b) {
  show(st, b->out[0], b->n, g_caps & RMP3_CAP_UM_P32 ? 0.5f : 0.375f);
  if (b->out[1])
    for (int32_t i = 0; i < b->n; ++i) b->out[1][i] = b->out[0][i];
}
static const rmp3_plugin g3 = {
  .magic = RMP3_MAGIC, .size = sizeof(rmp3_plugin), .minor = RMP3_MINOR,
  .kind = RMP3_KIND_MACHINE, .places = RMP3_AT_VOICE,
  .wants = RMP3_CAP_TRANSPORT | RMP3_CAP_GUARD | RMP3_CAP_UM_P32,
  .out_channels = 1,
  .render = f_render3,
};
#endif

RMP_ENTRY const rmp_plugin rmp_entry = {
  .magic = RMP_PLUGIN_MAGIC, .api = 2u,
  .state_bytes = sizeof(pp), .cpu_permille = 10,
  .init = f_init, .voice_init = f_voice_init, .note_on = f_note_on, .note_off = f_note_off, .params = f_params,
  .render = f_render2,
#ifndef PAGES_PLAIN2
  .reserved = { [RMP_P_V3] = RMP_FN(&g3), [RMP_P_PARAMS_HI] = RMP_FN(f_params_hi), [RMP_P_DRAW] = RMP_FN(f_draw) },
#else
  .reserved = { [RMP_P_PARAMS_HI] = RMP_FN(f_params_hi), [RMP_P_DRAW] = RMP_FN(f_draw) },
#endif
};
