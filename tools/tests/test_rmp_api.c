// tools/tests/test_rmp_api.c - the ABI's layout on the Mac matches the Teensy's
// 32-bit layout where it matters (offsets of the leading fields), and API 2's additions; API 3's tables
// (sdk/rmp/rmp_api3.h: sizes asserted by the header itself on AArch64, offsets here on any 64-bit host).
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "../../sdk/rmp/rmp_api.h"
#include "../../sdk/rmp/rmp_api3.h"
static int32_t le(void *st, const uint8_t *b, int32_t n, int16_t v[16]) { (void)st; (void)b; (void)n; (void)v; return 0; }
static void ve(void *st) { (void)st; }
static void ph(void *st, const float *p) { (void)st; (void)p; }
int main(void) {
  assert(offsetof(rmp_plugin, magic) == 0);
  assert(offsetof(rmp_plugin, api) == 4);
  assert(offsetof(rmp_plugin, state_bytes) == 8);
  assert(offsetof(rmp_plugin, cpu_permille) == 12);
  assert(offsetof(rmp_plugin, init) == 16);
  assert(offsetof(rmp_host, api) == 0);
  assert(RMP_API == 2u && RMP_API_MIN == 1u);
  assert(RMP_P_PARSE == 0 && RMP_P_LOAD_ENTRY == 1 && RMP_P_VOICE_ENTER == 2 && RMP_P_VOICE_LEAVE == 3);
  assert(RMP_P_PARAMS_HI == 5 && RMP_PARAMS_HI == 16 && RMP_P_V3 == 4);
  assert(RMP_H_POOL_INFO == 0 && RMP_H_POOL_READ == 1);
  assert(sizeof(rmp_pool_info) == 20 && offsetof(rmp_pool_info, head_frames) == 16);
  /* the accessors: a v1 table has no v2 functions, a v2 table the ones it fills */
  rmp_plugin p = {RMP_PLUGIN_MAGIC, 1u, 0, 0, 0, 0, 0, 0, 0, 0, {0, RMP_FN(le), RMP_FN(ve), RMP_FN(ve), 0, 0, 0, 0}};
  assert(rmp_load_entry_of(&p) == 0 && rmp_voice_enter_of(&p) == 0 && rmp_voice_leave_of(&p) == 0);
  p.api = 2u;
  assert(rmp_load_entry_of(&p) == le && rmp_voice_enter_of(&p) == ve && rmp_voice_leave_of(&p) == ve);
  /* params_hi (reserved[4]): NULL in a table without it, never read from an API-1 table */
  assert(rmp_params_hi_of(&p) == 0);
  p.reserved[RMP_P_PARAMS_HI] = RMP_FN(ph);
  assert(rmp_params_hi_of(&p) == ph);
  p.api = 1u;
  assert(rmp_params_hi_of(&p) == 0);
  p.api = 2u;
  /* stepped knobs: the ends, a round trip, a params() value */
  assert(rmp_step_v14(361, 0) == 0 && rmp_step_v14(361, 360) == RMP_STEP_V14_MAX && rmp_step_entry(361, 16383) == 360);
  for (uint32_t q = 0; q < 361; ++q) assert(rmp_step_entry(361, rmp_step_v14(361, q)) == q);
  assert(rmp_step_entry_of(361, 63.5f) == 180 && rmp_step_entry_of(2, 127.0f) == 1 && rmp_step_entry_of(2, 0.0f) == 0);
  rmp_host h = {1u, 48000.0f, 12, 0, 0, {0}};
  assert(rmp_pool_info_of(&h) == 0 && rmp_pool_read_of(&h) == 0);
  /* API 3: discovery through the reserved slots, the tables' field offsets (64-bit) */
  assert(RMP_H_V3 == 2 && RMP_P_V3 == 4 && RMP3_MAGIC == 0x33504D52u && RMP_API3 == 3u);
  rmp3_host h3 = {RMP3_MAGIC, sizeof(rmp3_host), RMP3_MINOR, RMP3_CAP_TRANSPORT, 4, 1500, 1u << 24, 0, {0}};
  h.api = 2u;
  assert(rmp3_host_of(&h) == 0);
  h.reserved[RMP_H_V3] = &h3;
  assert(rmp3_host_of(&h) == &h3);
  h.api = 1u;
  assert(rmp3_host_of(&h) == 0);   /* an API 1 table never has reserved[] */
  h.api = 2u, h3.magic = 0;
  assert(rmp3_host_of(&h) == 0);
  if (sizeof(void *) == 8) {
    assert(sizeof(rmp3_host) == 96 && sizeof(rmp3_block) == 112 && sizeof(rmp3_plugin) == 136);
    assert(sizeof(rmp3_setup) == 40 && sizeof(rmp3_voice) == 40 && sizeof(rmp3_transport) == 32);
    assert(offsetof(rmp_plugin, reserved) == 64 && offsetof(rmp3_block, out) == 16 && offsetof(rmp3_block, transport) == 80);
    assert(offsetof(rmp3_plugin, states) == 40 && offsetof(rmp3_plugin, out_channels) == 44 && offsetof(rmp3_plugin, sig_out) == 56);
    assert(offsetof(rmp3_plugin, setup) == 72 && offsetof(rmp3_plugin, render) == 88 && offsetof(rmp3_plugin, reserved) == 104);
    assert(RMP3_PLUGIN_BYTES_A64 == sizeof(rmp3_plugin));
  }
  printf("ok\n");
  return 0;
}
