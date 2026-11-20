// plugins/api3_test/fxprobe.c - Plugin API 3's test effect (core/tests/test_api3.cpp; Makefile V=fx): an API 3 only
// RMP3_KIND_EFFECT for the send position. P1 MODE in zones of 16 (b->p): 0 writes, on the left, frame 0 = 0.5 (it ran),
// frame 1 = the block's edge flags (START 1, STOP 2, JUMP 4) / 16, frame 2 = n / 64, the rest of L and all of R 0;
// 1 render faults; 2 render runs long (1.2 M loop steps: over its budget); 3 never returns (a hang); 4 runs ~30 ms and
// returns (a render fx_stop gives up on); 5 runs a little (50 k loop steps: over a small budget, under the default).
// V=fx16 (PROBE_FX16, fx16.json: P1..P16 but P14): also frames 3..14 of L = P5..P16 / 128 (the send slot's held knobs),
// frame 15 = 1 when the host has RMP3_CAP_FX_P16.
// V=fxdraw (PROBE_FXDRAW, fxdraw.json; fx16's render too): a draw for the SEND FX pages (sendfx-draw, rmp_api.h
// RMP_PAGE_FX1..FX3; core/tests/test_fx_draw.cpp): row 0 a bar page long, row 1 track + 1 (the send slot), row 2 pixel
// 0 = flags, 2 = a state was handed (it must not be), 4 = RMP3_CAP_FX_DRAW; rows 8..27 bars p[0..19] long (P1..P16,
// TYPE, ROUTE, BASE, WIDTH); rows 48..63 the pattern (x + 2y) % 5 == 0. P2 1 declines (0), P2 2 faults.
// MIT, Copyright 2026 trigcondition and contributors.
#include "rmp/rmp_api3.h"

typedef struct { int32_t calls; } fxp;
#if PROBE_FX16 || PROBE_FXDRAW
static uint32_t g_caps;   /* the host's RMP3_CAP_* (frame 15: RMP3_CAP_FX_P16) */
static int32_t f_init(const rmp_host *h) {
  const rmp3_host *x = rmp3_host_of(h);
  g_caps = x ? x->caps : 0u;
  return 0;
}
#else
static int32_t f_init(const rmp_host *h) { (void)h; return 0; }
#endif
static void f_render(void *st, const rmp3_block *b) {
  fxp *s = st;
  int mode = b->p ? (int)b->p[0] / 16 : 0;
  if (mode == 1) *(volatile int *)0 = 1;
  if (mode == 2)
    for (volatile uint32_t i = 0; i < 1200000u; ++i) {}
  if (mode == 3)
    for (;;) {}
  if (mode == 4)
    for (volatile uint32_t i = 0; i < 40000000u; ++i) {}
  if (mode == 5)
    for (volatile uint32_t i = 0; i < 50000u; ++i) {}
  ++s->calls;
  for (int32_t i = 0; i < b->n; ++i) b->out[0][i] = 0.0f, b->out[1][i] = 0.0f;
  uint32_t e = b->transport->flags;
  b->out[0][0] = 0.5f;
  b->out[0][1] = (float)((e & RMP3_T_START ? 1 : 0) | (e & RMP3_T_STOP ? 2 : 0) | (e & RMP3_T_JUMP ? 4 : 0)) / 16.0f;
  b->out[0][2] = (float)b->n / 64.0f;
#if PROBE_FX16 || PROBE_FXDRAW
  // fx16 (sendfx-pages, issue #15): P5..P16 as the host hands them, frames 3..14 = p[4..15] / 128 (a block of 15 or more)
  for (int32_t j = 4; j < 16 && j - 1 < b->n; ++j) b->out[0][j - 1] = b->p ? b->p[j] / 128.0f : -1.0f;
  if (b->n > 15) b->out[0][15] = g_caps & RMP3_CAP_FX_P16 ? 1.0f : 0.0f;
#endif
}
#if PROBE_FXDRAW
static void bar(uint8_t *fb, int32_t y, int32_t n) {
  for (int32_t x = 0; x < n && x < RMP_DRAW_W; ++x) fb[y * 16 + x / 8] |= (uint8_t)(0x80 >> (x % 8));
}
static int32_t f_draw(void *st, int32_t page, uint32_t flags, int32_t track, const float *p, uint8_t *fb) {
  if ((int32_t)p[1] == 1) return 0;
  if ((int32_t)p[1] == 2) *(volatile int *)0 = 1;
  bar(fb, 0, page);
  bar(fb, 1, track + 1);
  if (flags) fb[2 * 16] |= 0x80;
  if (st) fb[2 * 16] |= 0x20;
  if (g_caps & RMP3_CAP_FX_DRAW) fb[2 * 16] |= 0x08;
  for (int32_t k = 0; k < 20; ++k) bar(fb, 8 + k, (int32_t)p[k]);
  for (int32_t y = 48; y < RMP_DRAW_H; ++y)
    for (int32_t x = 0; x < RMP_DRAW_W; ++x)
      if ((x + 2 * y) % 5 == 0) fb[y * 16 + x / 8] |= (uint8_t)(0x80 >> (x % 8));
  return 1;
}
#endif
static const rmp3_plugin g3 = {
  .magic = RMP3_MAGIC, .size = sizeof(rmp3_plugin), .minor = RMP3_MINOR,
  .kind = RMP3_KIND_EFFECT, .places = RMP3_AT_SEND, .needs = RMP3_CAP_EFFECT,
  .in_channels = 1, .out_channels = 2, .render = f_render,
};
RMP_ENTRY const rmp_plugin rmp_entry = {
  .magic = RMP_PLUGIN_MAGIC, .api = 3u, .state_bytes = sizeof(fxp), .cpu_permille = 5,
#if PROBE_FXDRAW
  .init = f_init, .reserved = { [RMP_P_V3] = RMP_FN(&g3), [RMP_P_DRAW] = RMP_FN(f_draw) },
#else
  .init = f_init, .reserved = { [RMP_P_V3] = RMP_FN(&g3) },
#endif
};
