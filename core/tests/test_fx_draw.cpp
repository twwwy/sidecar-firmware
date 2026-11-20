// core/tests/test_fx_draw.cpp - an effect's pages (sendfx-draw; docs/um-pages.md "An effect's pages", sdk/rmp/rmp_api.h
// RMP_PAGE_FX1..FX3, core/ui/remote.cpp fx_frame / fx_tick, core/user/um.cpp on_fx_draw*, core/host_link.cpp 22 ver 4):
// a user effect's plugin draws the Rytm's SEND FX pages of the send slot that plays it, with real RMPA packages run in
// place on this arm64 Mac (plugins/api3_test fxprobe.c V=fxdraw: its draw puts the page, the track, the flags, whether
// it got a state and RMP3_CAP_FX_DRAW, P1..P20 as bars and a fixed pattern into the frame; P2 1 declines, P2 2
// faults; and the shipped effects PHASER / BBD / NIMBUS, which have no draw). Checked:
//   - the SEND FX view (29 <12 | 13 + (page - 1) << 4> <on>): the draw called with page RMP_PAGE_FX1, track = the send
//     slot, flags 0, state NULL, P1..P4 the slot's knobs, P5..P16 the effect's manifest defaults, P17..P20 TYPE ROUTE
//     BASE WIDTH; its frame sent whole, no text items; RMP3_CAP_FX_DRAW in the host's caps;
//   - pages 2 / 3 drawn with their page numbers; a page flipped back to with nothing changed is not drawn again (the
//     page cache: no plugin call, its frame at once); a redraw on a knob (P1..P4), on BASE / WIDTH, none on the same values;
//   - a send slot playing an effect without draw (PHASER): no draw, the Rytm's page (3A 0 0 after ours), as before;
//     the shipped packages load and play unchanged (no draw key);
//   - one view at a time: a track's 29 ends the SEND FX view and the other way round; the off of another page ignored;
//     malformed bytes (page 4, track 14, bit 6) dropped;
//   - fallback: a declined draw gives the stock page back (3A 0 0), drawn again after; a fault mutes the effect, stock,
//     no more draw calls;
//   - 22 ver 4 SENDFX DRAWS: 1 for the drawing slot after the Rytm's ASK, none for PHASER's; 0 once the effect is muted.
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "engine.h"
#include "fx/insert.h"
#include "fx/send_fx.h"
#include "hooks.h"
#include "host_link.h"
#include "rmp_api3.h"
#include "rmp_slots.h"
#include "ui/remote.h"
#include "ui/ui.h"
#include "um_host.h"
#include "user/um.h"

using namespace core;
using namespace core::ui;

#if defined(__aarch64__)
static std::vector<std::string> g_log;
static void sink(const char* s, int n) { g_log.emplace_back(s, (size_t)n); }
static std::vector<std::vector<uint8_t>> g_msgs;
static void out(const uint8_t* m, int n) { g_msgs.emplace_back(m, m + n); }
static std::string slurp(const std::string& p) {
  FILE* f = fopen(p.c_str(), "rb");
  CHECK(f);
  std::string b;
  char t[65536];
  size_t n;
  while ((n = fread(t, 1, sizeof t, f)) > 0) b.append(t, n);
  fclose(f);
  return b;
}
static void put(const std::string& p, const std::string& b) {
  FILE* f = fopen(host_path(p.c_str()).c_str(), "wb");
  CHECK(f);
  fwrite(b.data(), 1, b.size(), f);
  fclose(f);
}
static void sysex(std::vector<uint8_t> m) { engine_sysex(m.data(), (int)m.size()); }
static void idle_all() {
  int quiet = 0;
  for (int i = 0; i < 200000; ++i) {
    if (um::idle()) quiet = 0;
    else if (++quiet > 8) return;
  }
  CHECK(!"um::idle never settled");
}
static std::string cmd(const std::string& c) {
  char o[220];
  CHECK(um::command(c.c_str(), o, sizeof o));
  idle_all();
  return o;
}
static uint32_t g_ms = 1000;
static uint64_t g_fake_us = 1000000;   // host_fake_time's clock (now_ms: the host link's pacing)
static void host_advance_ms(int ms) { host_fake_time(true, g_fake_us += (uint64_t)ms * 1000); }
static void tick(int n) {
  for (int i = 0; i < n; ++i) remote_tick(g_ms += 30);
}
static void view(int tb, int on) { sysex({0xF0, 0x7D, 0x52, 0x54, 0x29, 1, (uint8_t)tb, (uint8_t)on, 0xF7}); }
static int px(int x, int y) { return remote_frame()[(7 - y / 8) + 8 * x] >> (y % 8) & 1; }
static int run(int y) {   // the lit pixels from x 0 on row y (a bar's length)
  int n = 0;
  while (n < kW && px(n, y)) ++n;
  return n;
}
static bool pattern_ok() {
  for (int y = 48; y < kH; ++y)
    for (int x = 0; x < kW; ++x)
      if (px(x, y) != ((x + 2 * y) % 5 == 0)) return false;
  return true;
}
static int texts() {
  uint8_t t[36 * kTextSlots];
  return sent_texts(t, kTextSlots);
}
static bool stock_asked() {   // 3A 0 0 since g_msgs was cleared: the Rytm's own page again
  for (auto& m : g_msgs)
    if (m.size() == 9 && m[4] == 0x3A && m[6] == 0 && m[7] == 0) return true;
  return false;
}
static int draws_msg(int fx) {   // the last 22 ver 4 for FX fx since g_msgs was cleared: its on byte, -1 none
  int on = -1;
  for (auto& m : g_msgs)
    if (m.size() == 9 && m[4] == 0x22 && m[5] == 4 && m[6] == fx) on = m[7];
  return on;
}
static void knobs(int sl, int type, int route, int p1, int p2, int p3, int p4) {
  const uint8_t k[fx::kKnobs] = {(uint8_t)type, (uint8_t)route, (uint8_t)p1, (uint8_t)p2, (uint8_t)p3, (uint8_t)p4};
  for (int j = 0; j < fx::kKnobs; ++j) fx::control.knob[sl][j] = k[j];
}
// fxdraw.json's P5..P16 defaults (P14 none: 0)
static int xdef(int p) { return p == 14 ? 0 : 35 + 7 * (p - 5); }

int main() {
  char tmpl[] = "/tmp/core-fxdraw-XXXXXX";
  CHECK(mkdtemp(tmpl));
  host_set_card(tmpl);
  host_set_log_sink(sink);
  host_fake_time(true, g_fake_us);
  Fs fs;
  CHECK(fs.begin() && fs.mkdir("/Samples") && fs.mkdir("/Plugins") && fs.mkdir("/.rytm/projects/FD"));
  put("/.rytm/project", "FD");
  for (const char* n : {"api3_fxdraw", "ufx_phaser", "ufx_bbd", "ufx_nimbus"})
    put(std::string("/Plugins/") + n + ".rmp", slurp(std::string("tests/api3_pkgs/") + n + ".a64.rmp"));
  hooks.to_rytm = ::out;
  CHECK(engine_begin());
  um::begin(host_um_exec());
  sysex({0xf0, 0x7d, 0x52, 0x54, 0x10, 'F', 'D', 0xf7});
  for (int i = 0; i < 100000 && project_loading(); ++i) idle_save();
  idle_all();
  cmd("um=empty");
  cmd("um=fx:1:fx.api3_fxdraw");
  cmd("um=fx:2:fx.phaser");
  CHECK(um::slot_view(5).state == RMP_SLOT_READY && um::slot_view(6).state == RMP_SLOT_READY);
  CHECK(hooks.fx_draw_key && hooks.fx_draw_values && hooks.fx_draw);
  knobs(0, fx::kFirstUser + 0, 70, 10, 20, 30, 40);   // SEND FX 1: the probe, ROUTE POST
  knobs(1, fx::kFirstUser + 1, 0, 64, 64, 64, 64);    // SEND FX 2: PHASER
  CHECK(hooks.fx_draw_key(0) != 0 && hooks.fx_draw_key(1) == 0);

  // ---- the shipped effects load and play as before: no draw, no key; their labels unchanged
  cmd("um=fx:3:fx.bbd");
  cmd("um=fx:4:fx.nimbus");
  CHECK(um::slot_view(7).state == RMP_SLOT_READY && um::slot_view(8).state == RMP_SLOT_READY);
  CHECK(!um::slot_view(6).muted && !um::slot_view(7).muted && !um::slot_view(8).muted);
  for (int u = 1; u < 4; ++u) {
    knobs(1, fx::kFirstUser + u, 0, 64, 64, 64, 64);
    CHECK(hooks.fx_draw_key(1) == 0);
  }
  knobs(1, fx::kFirstUser + 1, 0, 64, 64, 64, 64);
  printf("test_fx_draw: PHASER, BBD, NIMBUS load unchanged (READY, no draw)\n");

  // ---- SEND FX 1 page 1: page 5, track 0, no flags, no state, the caps; P1..P4, P5..P16 the defaults, TYPE..WIDTH
  const uint32_t f0 = remote_stats().src_frames, d0 = remote_stats().user_draws;
  view(12, 1);
  tick(3);
  CHECK(remote_stats().user_draws == d0 + 1 && remote_stats().user_drawn >= 1 && remote_stats().src_frames == f0 + 1);
  CHECK(run(0) == 5 && run(1) == 1);
  CHECK(!px(0, 2) && !px(2, 2) && px(4, 2));   // flags 0; state NULL; RMP3_CAP_FX_DRAW
  CHECK(run(8) == 10 && run(9) == 20 && run(10) == 30 && run(11) == 40);
  for (int p = 5; p <= 16; ++p) CHECK(run(8 + p - 1) == xdef(p));
  CHECK(run(24) == fx::kFirstUser && run(25) == 70 && run(26) == 0 && run(27) == 127);   // TYPE, ROUTE, BASE, WIDTH
  CHECK(pattern_ok() && texts() == 0);
  printf("test_fx_draw: SEND FX 1 page 1 drawn: page 5, slot 0, state NULL, P1..P16, TYPE ROUTE BASE WIDTH\n");

  // ---- pages 2 / 3, then back: the kept frame, no draw
  view(12 | 0x10, 1);
  tick(3);
  CHECK(run(0) == 6 && remote_stats().user_draws == d0 + 2);
  view(12 | 0x20, 1);
  tick(3);
  CHECK(run(0) == 7 && remote_stats().user_draws == d0 + 3);
  const uint32_t c0 = remote_stats().fx_cached;
  view(12, 1);
  tick(1);
  CHECK(run(0) == 5 && remote_stats().user_draws == d0 + 3 && remote_stats().fx_cached == c0 + 1);
  view(12 | 0x10, 1);
  tick(1);
  CHECK(run(0) == 6 && remote_stats().user_draws == d0 + 3 && remote_stats().fx_cached == c0 + 2);
  view(12, 1);
  tick(1);
  CHECK(run(0) == 5 && remote_stats().user_draws == d0 + 3);
  printf("test_fx_draw: pages 2 / 3 drawn (6, 7); a page flipped back to: its kept frame at once, no draw\n");

  // ---- redraws: a knob, the same values (none), BASE / WIDTH
  fx::control.knob[0][2] = 77;
  tick(2);
  CHECK(remote_stats().user_draws == d0 + 4 && run(8) == 77);
  tick(4);
  CHECK(remote_stats().user_draws == d0 + 4);
  ins::set_eq_bw(ins::kBus0 + 0, 20, 50);
  tick(2);
  int b = 0, w = 0;
  ins::eq_bw(ins::kBus0 + 0, &b, &w);
  CHECK(remote_stats().user_draws == d0 + 5 && run(26) == b && run(27) == w && b == 20);
  printf("test_fx_draw: redrawn on a knob and on BASE / WIDTH, not on the same values\n");

  // ---- SEND FX 2 plays PHASER (no draw): the Rytm's page, 3A 0 0 after ours; the draws count unchanged
  g_msgs.clear();
  const uint32_t d5 = remote_stats().user_draws;
  view(13, 1);
  tick(3);
  CHECK(stock_asked() && remote_stats().user_draws == d5);
  printf("test_fx_draw: SEND FX 2 on PHASER (no draw): the Rytm's own page, as before\n");

  // ---- one view at a time; the off of another page; malformed
  view(12, 1);
  tick(2);
  CHECK(run(0) == 5);
  view(0, 1);   // BD's SRC page (a built-in machine): the SEND FX view ends
  tick(2);
  CHECK(remote_src_track() == 0);
  view(12, 1);   // and back: the track's view ends
  tick(2);
  CHECK(remote_src_track() < 0 && run(0) == 5);
  const uint32_t d6 = remote_stats().user_draws;
  view(12 | 0x10, 0);   // page 2's off while page 1 shows: not this view
  tick(2);
  CHECK(run(0) == 5 && remote_stats().user_draws == d6);
  const uint32_t bad = remote_stats().bad;
  view(12 | 0x30, 1), view(14, 1), view(0x40 | 12, 1);
  CHECK(remote_stats().bad == bad + 3);
  tick(2);
  CHECK(run(0) == 5);
  printf("test_fx_draw: one view at a time (a track's and SEND FX's replace each other), another page's off ignored, malformed dropped\n");

  // ---- 22 ver 4 SENDFX DRAWS: 1 for SEND FX 1 after the Rytm's ASK, nothing for SEND FX 2 (PHASER)
  g_msgs.clear();
  sysex({0xF0, 0x7D, 0x52, 0x54, 0x23, 1, 0xF7});
  for (int i = 0; i < 4; ++i) sendfx_step();
  CHECK(draws_msg(0) == 1 && draws_msg(1) == -1);
  printf("test_fx_draw: 22 ver 4: SEND FX 1 draws (1), SEND FX 2 (PHASER) nothing sent\n");

  // ---- fallback: declined (P2 1), back, a fault (P2 2): muted, stock, no more calls, 22 ver 4 says 0
  g_msgs.clear();
  fx::control.knob[0][3] = 1;
  tick(3);
  CHECK(stock_asked());
  fx::control.knob[0][3] = 20;
  tick(3);
  CHECK(run(0) == 5 && run(9) == 20);
  g_msgs.clear();
  fx::control.knob[0][3] = 2;
  tick(3);
  CHECK(stock_asked() && um::slot_view(5).muted && hooks.fx_draw_key(0) == 0);
  const uint32_t d7 = remote_stats().user_draws;
  fx::control.knob[0][3] = 21;
  tick(4);
  CHECK(remote_stats().user_draws == d7);
  host_advance_ms(50);
  for (int i = 0; i < 4; ++i) sendfx_step();
  CHECK(draws_msg(0) == 0);
  printf("test_fx_draw: declined: stock and back; a fault mutes the effect (stock, no more calls, 22 ver 4 0)\n");

  printf("test_fx_draw: PASS\n");
  return 0;
}
#else
int main() {
  printf("test_fx_draw: skipped (needs an arm64 host to run the AArch64 packages)\n");
  return 0;
}
#endif
