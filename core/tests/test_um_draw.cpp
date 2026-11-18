// core/tests/test_um_draw.cpp - plugin-drawn screens (docs/um-pages.md "Plugin-drawn screens"; sdk/rmp/rmp_api.h
// RMP_P_DRAW, core/ui/remote.cpp user_frame, core/user/um.cpp on_draw): a user machine's plugin draws the Rytm's SRC /
// SMPL view of a track that plays it, with real RMPA packages run in place on this arm64 Mac (plugins/api3_test
// pagesprobe.c: api3_pages dual API 3, v2_pages plain API 2; its draw puts the page, the flags, the track, P1..P32 as
// bars and a fixed pattern into the frame; P2 1 declines, P2 2 faults). Checked:
//   - rows_to_fb: a row-major bitmap into the canvas layout (a known pattern, the corners);
//   - the SRC view (29 t 1): the draw called with page 1, the 2A's P1..P16 (the fraction too), P17..P32 the voice's
//     (the manifest's defaults, P31 none 0), the voice's state, the track; its frame sent, no text items;
//   - a redraw on a 2A that changes a value (none on the same values), on a held step's (RMP_DRAW_LOCKED, and back), on
//     a P17..P32 change (umknob); none while nothing changes;
//   - the view's pages (29 <t | (page - 1) << 4> <on>): SMPL, SRC 2, SMPL 2 drawn with their page number, the off of
//     another page ignored, a malformed track byte dropped; a built-in machine's SMPL view not drawn;
//   - API 2 (v2_pages) drawing with no voice on the machine yet (state NULL);
//   - a plugin without draw (api3_test): nothing drawn, the stock page (no frame), as before;
//   - fallback: the draw declining (0) gives the stock page back (3A 0 0), drawing again after; a fault mutes the slot
//     (the guard) and the stock page comes back, the draw not called again;
//   - RMP3_CAP_DRAW in the host's caps (um::begin's log line).
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "engine.h"
#include "hooks.h"
#include "host_link.h"
#include "rmp_api3.h"
#include "rmp_slots.h"
#include "ui/remote.h"
#include "ui/ui.h"
#include "um_host.h"
#include "user/um.h"
#include "voices.h"

using namespace core;
using namespace core::ui;

// ---- rows_to_fb: any host
static void rows_test() {
  uint8_t rows[kBytes] = {}, fb[kBytes];
  auto lit = [&](int x, int y) { rows[16 * y + x / 8] |= (uint8_t)(0x80 >> (x % 8)); };
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x)
      if ((x * 7 + y * 3) % 11 == 0 || x == y) lit(x, y);
  lit(127, 0), lit(0, 63), lit(127, 63);
  memset(fb, 0xAA, sizeof fb);
  rows_to_fb(rows, fb);
  Canvas c;
  c.clear();
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x)
      if (rows[16 * y + x / 8] >> (7 - x % 8) & 1) c.pixel(x, y);
  CHECK(memcmp(c.fb, fb, kBytes) == 0);
  CHECK(fb[7] & 1);                         // (0, 0): byte 7, bit 0
  CHECK(fb[7 + 8 * 127] & 1);               // (127, 0)
  CHECK(fb[0] >> 7 & 1);                    // (0, 63): byte 0, bit 7
  CHECK(fb[8 * 127] >> 7 & 1);              // (127, 63)
  CHECK((fb[6 + 8 * 9] >> 1 & 1) == 1);     // (9, 9) on the diagonal: band 1 -> byte 6, bit 1
  printf("test_um_draw: rows_to_fb matches Canvas::pixel, the corners\n");
}

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
static void tick(int n) {
  for (int i = 0; i < n; ++i) remote_tick(g_ms += 30);
}
// a 0x04 for voice vi on slot s: P1..P16 at 64 but P2 100
static void report(int s, int vi) {
  std::vector<uint8_t> m = {0xf0, 0x7d, 0x52, 0x54, 0x04, (uint8_t)(0x50 + 8 * s + vi), 60, 100};
  for (int k = 0; k < 16; ++k) m.push_back(k == 1 ? 100 : 64), m.push_back(0);
  m.push_back(0xf7);
  sysex(m);
  idle_all();
}
// 2A: track t's (| 0x40 a held step's) P1..P16, each value << 7 | fraction
static void src_vals(int t, const std::vector<int>& p14) {
  std::vector<uint8_t> m = {0xF0, 0x7D, 0x52, 0x54, 0x2A, 1, (uint8_t)t, 0, 16};
  for (int k = 0; k < 16; ++k) {
    int v = k < (int)p14.size() ? p14[k] : 0;
    m.push_back((uint8_t)(v >> 7)), m.push_back((uint8_t)(v & 0x7F));
  }
  m.push_back(0xF7);
  sysex(m);
}
static void one_val(int t, int k, int v) {   // 2A of one value (a knob's turn)
  sysex({0xF0, 0x7D, 0x52, 0x54, 0x2A, 1, (uint8_t)t, (uint8_t)k, 1, (uint8_t)v, 0, 0xF7});
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
static int def(int p) { return p == 31 ? 0 : p == 20 ? 1 : 10 + 7 * (p - 17); }   // pages.json's knobs_hi defaults

int main() {
  rows_test();
  char tmpl[] = "/tmp/core-umdraw-XXXXXX";
  CHECK(mkdtemp(tmpl));
  host_set_card(tmpl);
  host_set_log_sink(sink);
  host_fake_time(true, 1000000);
  Fs fs;
  CHECK(fs.begin() && fs.mkdir("/Samples") && fs.mkdir("/Plugins") && fs.mkdir("/.rytm/projects/PD"));
  put("/.rytm/project", "PD");
  for (const char* n : {"api3_pages", "v2_pages", "api3_test"})
    put(std::string("/Plugins/") + n + ".rmp", slurp(std::string("tests/api3_pkgs/") + n + ".a64.rmp"));
  hooks.to_rytm = ::out;
  CHECK(engine_begin());
  um::begin(host_um_exec());
  sysex({0xf0, 0x7d, 0x52, 0x54, 0x10, 'P', 'D', 0xf7});
  for (int i = 0; i < 100000 && project_loading(); ++i) idle_save();
  idle_all();
  cmd("um=empty");
  cmd("um=assign:1:rytm.api3_pages");
  cmd("um=assign:2:rytm.v2_pages");
  cmd("um=assign:3:rytm.api3_test");
  CHECK(um::slot_view(0).state == RMP_SLOT_READY && um::slot_view(1).state == RMP_SLOT_READY &&
        um::slot_view(2).state == RMP_SLOT_READY);
  CHECK(hooks.user_draw && hooks.user_draw_key);
  {
    bool caps = false;   // RMP3_CAP_DRAW in the host's caps (um::begin's line)
    for (auto& l : g_log) caps |= l.find("host caps:") != std::string::npos && l.find("machine P17..P32, draw") != std::string::npos;
    if (!caps)
      for (auto& l : g_log) printf("  log: %s\n", l.c_str());
    CHECK(caps);
  }

  // the Rytm's 33: BD (voice 0) on U1, SD (voice 2) on U3, BT (voice 1) on U2, LT (voice 3) on 6-OP FM
  sysex({0xF0, 0x7D, 0x52, 0x54, 0x33, 1, 0, 6, 0, 0x50 | 0, 0, 0x60 | 2, 0, 0, 0, 0, 0, 0x58 | 1, 0, kFm | 3, 0xF7});
  CHECK(remote_track_machine(0) == 0x50 && remote_track_machine(4) == (0x58 | 1) && remote_track_machine(1) == (0x60 | 2));
  report(0, 0);   // voice 0 plays U1: its state exists, P17..P32 the defaults

  // ---- the SRC view of BD: page 1, the 2A's values, P17..P32 the voice's, the state, track 0
  std::vector<int> pv = {10 << 7 | 64, 100 << 7, 5 << 7, 64 << 7, 64 << 7, 20 << 7, 0, 0, 30 << 7, 0, 0, 0, 0, 0, 0, 127 << 7};
  src_vals(0, pv);
  const uint32_t f0 = remote_stats().src_frames;
  view(0, 1);
  tick(3);
  CHECK(remote_stats().page == 4 && remote_stats().user_draws == 1 && remote_stats().user_drawn == 1);
  CHECK(remote_stats().src_frames == f0 + 1);
  CHECK(run(0) == 1 && px(127, 0));            // page 1
  CHECK(!px(0, 1) && px(2, 1));                // not locked; a state
  CHECK(run(2) == 1);                          // track 0
  CHECK(run(8) == 10 && run(9) == 100 && run(10) == 5 && run(13) == 20 && run(16) == 30 && run(23) == 127);
  CHECK(run(40) == 64);                        // P1's fraction 64/128
  for (int p = 17; p <= 32; ++p) CHECK(run(8 + p - 1) == def(p));
  CHECK(pattern_ok() && texts() == 0);
  printf("test_um_draw: SRC view drawn by U1 (API 3): page 1, P1..P16 with the fraction, P17..P32, state, track 0\n");

  // ---- redraws: a knob's 2A, the same values again (none), a held step's, P17..P32 (umknob), nothing (none)
  one_val(0, 4, 77);
  tick(2);
  CHECK(remote_stats().user_draws == 2 && run(12) == 77);
  one_val(0, 4, 77);
  tick(4);
  CHECK(remote_stats().user_draws == 2);
  {
    std::vector<int> held = pv;
    held[0] = 3 << 7;
    src_vals(0x40 | 0, held);   // a held step's values
    tick(2);
    CHECK(remote_stats().user_draws == 3 && px(0, 1) && run(8) == 3);
    src_vals(0, pv);            // let go: the sound's
    tick(2);
    CHECK(remote_stats().user_draws == 4 && !px(0, 1) && run(8) == 10 && run(12) == 64);
  }
  CHECK(cmd("umknob=1:17:50").find("V1 U1") != std::string::npos);
  tick(2);
  CHECK(remote_stats().user_draws == 5 && run(8 + 16) == 50);
  tick(10);
  CHECK(remote_stats().user_draws == 5);
  printf("test_um_draw: redrawn on a 2A, a held step's (LOCKED) and back, a P17 change; not on the same values\n");

  // ---- the view's pages: SMPL 2, SRC 2 3, SMPL 2 4; an off of another page ignored; malformed dropped
  for (int pg = 2; pg <= 4; ++pg) {
    view(0 | (pg - 1) << 4, 1);
    tick(3);
    CHECK(remote_src_track() == 0 && run(0) == pg && run(8 + 16) == 50 && pattern_ok());
  }
  const uint32_t d4 = remote_stats().user_draws;
  view(0, 0);   // (page 1's off while page 4 shows: not this view)
  tick(2);
  CHECK(remote_src_track() == 0 && remote_stats().user_draws == d4);
  const uint32_t bad = remote_stats().bad;
  view(0x40, 1), view(12, 1), view(0x1C, 1);
  CHECK(remote_stats().bad == bad + 3 && remote_src_track() == 0);
  view(0x30, 0);
  CHECK(remote_src_track() < 0);
  // a built-in machine's SMPL view: nothing drawn (the Rytm keeps its page)
  const uint32_t f1 = remote_stats().src_frames;
  view(5 | 0x10, 1);
  tick(4);
  CHECK(remote_stats().src_frames == f1 && remote_stats().user_draws == d4);
  view(5 | 0x10, 0);
  printf("test_um_draw: SMPL / SRC 2 / SMPL 2 pages drawn with their number; offs and malformed bytes; FM's SMPL stock\n");

  // ---- API 2 (v2_pages on BT, voice 1): no voice plays it yet - state NULL, P17..P32 the manifest's defaults
  src_vals(4, {7 << 7, 100 << 7});
  view(4, 1);
  tick(3);
  CHECK(remote_stats().user_draws == d4 + 1 && run(0) == 1 && run(2) == 5 && !px(2, 1) && run(8) == 7 && run(8 + 16) == def(17));
  view(4, 0);
  printf("test_um_draw: API 2 machine drawn, no voice yet (state NULL), defaults for P17..P32\n");

  // ---- a plugin without draw (U3, api3_test, on SD): no draw, no frame (the Rytm's stock page, as before)
  {
    const uint32_t f = remote_stats().src_frames, d = remote_stats().user_draws;
    src_vals(1, {64 << 7, 64 << 7});
    view(1, 1);
    tick(4);
    CHECK(remote_stats().src_frames == f && remote_stats().user_draws == d);
    view(1, 0);
  }
  printf("test_um_draw: a plugin without draw: nothing drawn, stock\n");

  // ---- fallback: the draw declines (P2 1): stock again (3A 0 0); P2 back: drawn again
  view(0, 1);
  tick(3);
  CHECK(run(0) == 1);
  g_msgs.clear();
  one_val(0, 1, 1);
  tick(3);
  CHECK(stock_asked());
  const uint32_t f2 = remote_stats().src_frames;
  one_val(0, 1, 100);
  tick(3);
  CHECK(remote_stats().src_frames > f2 && run(0) == 1 && run(9) == 100);
  printf("test_um_draw: a declined draw gives stock back (3A 0 0), drawn again after\n");

  // ---- a fault (P2 2): the guard mutes U1, stock again, the draw not called again
  g_msgs.clear();
  g_log.clear();
  one_val(0, 1, 2);
  tick(3);
  CHECK(um::slot_view(0).muted && stock_asked());
  {
    bool said = false;
    for (auto& l : g_log) said |= l.find("muted: crashed in draw") != std::string::npos;
    CHECK(said);
  }
  const uint32_t d5 = remote_stats().user_draws, f3 = remote_stats().src_frames;
  one_val(0, 1, 100);
  tick(4);
  CHECK(remote_stats().user_draws == d5 && remote_stats().src_frames == f3);
  view(0, 0);
  printf("test_um_draw: a faulting draw mutes U1 (guard), stock, no more draws\n");
  printf("test_um_draw: PASS\n");
  return 0;
}
#else
int main() {
  rows_test();
  printf("test_um_draw: the packages are AArch64 - only rows_to_fb here\n");
  return 0;
}
#endif
