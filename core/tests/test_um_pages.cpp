// core/tests/test_um_pages.cpp - a user machine's P17..P32 and stepped knobs (0.5.0-pages, docs/um-pages.md;
// core/um_hi.h, core/user/um.cpp, rmp_proto.h 52 UM PAGES) with real RMPA packages run in place on this arm64 Mac:
// plugins/api3_test's pagesprobe (pages.json: P1..P16, knobs_hi P17..P32 but P31, P4 a 361-entry pair knob, P5 a
// stepped number of 5) as a dual package (api3_pages, its API 3 render) and a plain API 2 one (v2_pages), built with
// `make -C plugins/api3_test V=pages a64` / `V=pages2` and kept in tests/api3_pkgs. Checked:
//   - what the Rytm is told after its 1A: 52 HEAD (counts, pages, titles), HI KNOBS, STEPS, PAIR NAMES, a slot without
//     the block a HEAD of none; P17..P32's list names on a 19 ASK ENUM arg 16..31;
//   - the values: the manifest's defaults when a voice's machine changes (a P without a knob 0), what params_hi hands
//     the plugin (API 3 and API 2: before the first render, then on change), every poly instance of the voice too;
//   - 52 VALUES both ways: the Rytm's turn taken (no echo), ours after its ASK / a hello / a change, refusals (another
//     slot, past P32, a voice 8, another version);
//   - the kit file: UMX lines written after a turn, read back on the kit's load, another kit's load the defaults again;
//   - umknob=V:P:VAL, the stepped knobs' text (hooks.user_value_text), RMP3_CAP_UM_P32 in the host's caps.
#include <sys/stat.h>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "engine.h"
#include "fx/insert.h"
#include "store/store.h"
#include "hooks.h"
#include "host_link.h"
#include "pool/pool.h"
#include "rmp_api3.h"
#include "rmp_format.h"
#include "rmp_proto.h"
#include "rmp_slots.h"
#include "um_host.h"
#include "user/um.h"
#include "um_hi.h"
#include "voices.h"

using namespace core;

#if defined(__aarch64__)
static std::vector<std::string> g_log;
static void sink(const char* s, int n) { g_log.emplace_back(s, (size_t)n); }
static std::vector<std::vector<uint8_t>> g_msgs;   // what the engine sent the Rytm
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
  for (size_t i = g_log.size() > 12 ? g_log.size() - 12 : 0; i < g_log.size(); ++i) fprintf(stderr, "  log: %s\n", g_log[i].c_str());
  CHECK(!"um::idle never settled");
}
static std::string cmd(const std::string& c) {
  char o[220];
  CHECK(um::command(c.c_str(), o, sizeof o));
  idle_all();
  return o;
}
// 0x04 (note >= 0) or 0x03 for voice vi on slot s (0..4): P1..P16 as 14-bit values (value << 7 | fraction)
static void report(int s, int vi, int note, const std::vector<int>& p14) {
  std::vector<uint8_t> m = {0xf0, 0x7d, 0x52, 0x54, (uint8_t)(note >= 0 ? 0x04 : 0x03), (uint8_t)(0x50 + 8 * s + vi)};
  if (note >= 0) m.push_back((uint8_t)note), m.push_back(100);
  for (int k = 0; k < 16; ++k) {
    int v = k < (int)p14.size() ? p14[k] : 64 << 7;
    m.push_back((uint8_t)(v >> 7)), m.push_back((uint8_t)(v & 0x7f));
  }
  m.push_back(0xf7);
  sysex(m);
  idle_all();
}
static std::vector<int> view(int v) { return {0, 100 << 7, v << 7, 8128, 64 << 7}; }   // P3 the probe's view, P4 entry 180
static uint64_t g_t = 1000000;
static int16_t g_ring[1024][kVoices];
static uint64_t g_mach[16];
static int g_w = 0;
static std::vector<float> block(int vi) {
  g_t += 250;
  host_fake_time(true, g_t);
  um::pre_block();
  render_voices(g_ring, 1024, g_w, 0, kVoices, g_mach);
  std::vector<float> r(kBlock);
  for (int k = 0; k < kBlock; ++k) r[k] = g_ring[(g_w + k) % 1024][vi] / (core::kVoiceGain * 0.7f * 32767.0f);
  g_w = (g_w + kBlock) % 1024;
  return r;
}
static bool near(float a, float b) { return std::fabs(a - b) < 0.002f; }
static std::vector<std::vector<uint8_t>> sent(int kind) {   // the 52s of this kind sent since g_msgs was cleared
  std::vector<std::vector<uint8_t>> r;
  for (auto& m : g_msgs)
    if (m.size() > 8 && m[4] == 0x52 && m[5] == 1 && m[6] == kind) r.push_back(m);
  return r;
}
static std::vector<uint8_t> values(int vi, int slot, int first, std::vector<int> v, int ver = 1) {
  std::vector<uint8_t> m = {0xf0, 0x7d, 0x52, 0x54, 0x52, (uint8_t)ver, 4, (uint8_t)vi, (uint8_t)slot, (uint8_t)first, (uint8_t)v.size()};
  for (int x : v) m.push_back((uint8_t)x);
  m.push_back(0xf7);
  return m;
}
static int def(int p) { return p == 31 ? 0 : p == 20 ? 1 : 10 + 7 * (p - 17); }   // pages.json's knobs_hi defaults
static std::string file(const std::string& p) {
  std::string s;
  FILE* f = fopen(p.c_str(), "rb");
  if (!f) return s;
  char b[512];
  for (size_t n; (n = fread(b, 1, sizeof b, f)) > 0;) s.append(b, n);
  fclose(f);
  return s;
}

int main() {
  char tmpl[] = "/tmp/core-umpages-XXXXXX";
  CHECK(mkdtemp(tmpl));
  host_set_card(tmpl);
  host_set_log_sink(sink);
  host_fake_time(true, g_t);
  Fs fs;
  CHECK(fs.begin() && fs.mkdir("/Samples") && fs.mkdir("/Plugins") && fs.mkdir("/.rytm/projects/PG"));
  put("/.rytm/project", "PG");
  for (const char* n : {"api3_pages", "v2_pages", "api3_test"})
    put(std::string("/Plugins/") + n + ".rmp", slurp(std::string("tests/api3_pkgs/") + n + ".a64.rmp"));
  hooks.to_rytm = ::out;
  CHECK(engine_begin());
  um::begin(host_um_exec());
  sysex({0xf0, 0x7d, 0x52, 0x54, 0x10, 'P', 'G', 0xf7});
  for (int i = 0; i < 100000 && project_loading(); ++i) idle_save();
  idle_all();
  cmd("um=empty");
  cmd("um=assign:1:rytm.api3_pages");
  cmd("um=assign:2:rytm.v2_pages");
  cmd("um=assign:3:rytm.api3_test");
  CHECK(um::slot_view(0).state == RMP_SLOT_READY && um::slot_view(0).api == 3);
  CHECK(um::slot_view(1).state == RMP_SLOT_READY && um::slot_view(1).api == 2);
  CHECK(um::slot_view(2).state == RMP_SLOT_READY);
  {
    char o[220];
    CHECK(um::command("um", o, sizeof o));
  }

  // ---- what the Rytm is told after its 1A (19 ASK META): the pages block of U1, none for U3
  g_msgs.clear();
  sysex({0xf0, 0x7d, 0x52, 0x54, 0x19, 0x01, 0x01, 0x00, 0x00, 0x01, 0xf7});
  idle_all();
  {
    auto h = sent(0);
    CHECK(h.size() == 1 && h[0].size() == 40);
    const auto& m = h[0];
    CHECK(m[7] == 1 && m[9] == 15 && m[10] == 3 && m[11] == 32 && m[12] == 2);   // U1: 15 knobs, both pages, P32, 2 stepped
    CHECK(memcmp(&m[13], "SRC 2 TITLE\0\0", 13) == 0 && memcmp(&m[26], "SMPL 2 TITLE\0", 13) == 0);
    int at1a = -1, athead = -1;
    for (int i = 0; i < (int)g_msgs.size(); ++i) {
      if (g_msgs[i][4] == 0x1A && g_msgs[i][8] == 0 && at1a < 0) at1a = i;
      if (g_msgs[i][4] == 0x52 && g_msgs[i][6] == 0) athead = i;
    }
    CHECK(at1a >= 0 && athead > at1a && g_msgs[at1a][7] == m[8]);   // after the 1A part 0, its gen
    auto k = sent(1);
    CHECK(k.size() == 4 && k[0].size() == 107 && k[3].size() == 7 + 3 + 3 * 24 + 1);
    CHECK(k[0][10] == 17 && k[0][16] == 10 && memcmp(&k[0][18], "High 17", 7) == 0 && memcmp(&k[0][30], "H17", 3) == 0);
    CHECK(k[0][10 + 3 * 24] == 20 && k[0][11 + 3 * 24] == RMP_LOOK_LIST && k[0][17 + 3 * 24] == 3);   // P20: a list of 3
    CHECK(k[3][10 + 2 * 24] == 32 && k[3][16 + 2 * 24] == 115);   // the last: P32 (P31 none)
    auto st = sent(2);
    CHECK(st.size() == 1 && st[0].size() == 7 + 3 + 2 * 7 + 1 && st[0][9] == 2);
    CHECK(st[0][10] == 4 && st[0][11] == RMP_STEP_PAIR && (st[0][12] << 7 | st[0][13]) == 361 &&
          (st[0][14] << 7 | st[0][15]) == 8128 && st[0][16] == 19);
    CHECK(st[0][17] == 5 && st[0][18] == RMP_STEP_NUMBER && (st[0][19] << 7 | st[0][20]) == 5 &&
          (uint32_t)(st[0][21] << 7 | st[0][22]) == rmp_step_v14(5, rmp_step_entry(5, 64 << 7)) && st[0][23] == 0);
    auto pn = sent(3);
    CHECK(pn.size() == 2 && pn[0][9] == 4 && pn[0][10] == 19 && pn[0][11] == 0 && pn[0][12] == 12 && pn[1][11] == 12 &&
          pn[1][12] == 7 && pn[1].size() == 7 + 6 + 7 * 7 + 1);
    CHECK(memcmp(&pn[0][13], "N1\0\0\0\0\0", 7) == 0 && memcmp(&pn[1][13 + 6 * 7], "N19\0\0\0\0", 7) == 0);
  }
  g_msgs.clear();
  sysex({0xf0, 0x7d, 0x52, 0x54, 0x19, 0x01, 0x03, 0x00, 0x00, 0x01, 0xf7});   // U3: a package without the block
  idle_all();
  CHECK(sent(0).size() == 1 && sent(0)[0][7] == 3 && sent(0)[0][9] == 0 && sent(0)[0][10] == 0 && sent(0)[0][12] == 0 &&
        sent(1).empty() && sent(2).empty() && sent(3).empty());
  g_msgs.clear();   // P20's names: 19 ASK ENUM arg 19 -> 1B list 19
  sysex({0xf0, 0x7d, 0x52, 0x54, 0x19, 0x01, 0x01, 0x00, 0x01, 19, 0xf7});
  CHECK(g_msgs.size() == 1 && g_msgs[0][4] == 0x1B && g_msgs[0][7] == 19 && g_msgs[0][10] == 3 &&
        memcmp(&g_msgs[0][12], "Sine", 4) == 0 && memcmp(&g_msgs[0][12 + 14 * 2], "Saw", 3) == 0);
  printf("test_um_pages: 52 HEAD / HI KNOBS / STEPS / PAIR NAMES after the 1A, P20's names on ASK ENUM 19\n");

  // ---- a voice on U1 (API 3): its defaults, handed before the first render; the Rytm told after its 1A (up)
  g_msgs.clear();
  report(0, 0, 60, view(0));
  CHECK(umhi::track[0].slot == 0 && umhi::track[0].n == 16 && umhi::track[0].tag == um::slot_view(0).fp);
  for (int j = 0; j < 16; ++j) CHECK(umhi::track[0].v[j] == def(17 + j));
  {
    auto o = block(0);
    for (int j = 0; j < 12; ++j) CHECK(near(o[j], def(17 + j) / 128.0f));
  }
  report(0, 0, -1, view(100));
  {
    auto o = block(0);
    CHECK(near(o[0], def(29) / 128.0f) && near(o[2], 0.0f) && near(o[3], 115 / 128.0f));   // P31: no knob, 0
    CHECK(near(o[4], 180 / 512.0f) && near(o[5], 1 / 256.0f) && near(o[6], 0.5f) && near(o[7], 2 / 8.0f));   // one call; UM_P32
  }
  {
    auto v = sent(4);
    CHECK(v.size() == 1 && v[0].size() == 28 && v[0][7] == 0 && v[0][8] == 1 && v[0][9] == 17 && v[0][10] == 16 &&
          v[0][11] == 10 && v[0][25] == 0 && v[0][26] == 115);
  }
  printf("test_um_pages: API 3: defaults before the first render (P31 0), RMP3_CAP_UM_P32, 52 VALUES out\n");

  // ---- the Rytm's turn (52 VALUES): taken, the plugin handed it, no echo; refusals
  g_msgs.clear();
  sysex(values(0, 1, 30, {99, 7}));
  idle_all();
  CHECK(umhi::track[0].v[13] == 99 && umhi::track[0].v[14] == 7 && sent(4).empty());
  {
    auto o = block(0);
    CHECK(near(o[1], 99 / 128.0f) && near(o[2], 7 / 128.0f) && near(o[5], 2 / 256.0f));
  }
  sysex(values(0, 2, 17, {1}));            // another slot than the voice's machine: not taken
  sysex(values(0, 1, 32, {1, 2}));         // past P32
  sysex(values(8, 1, 17, {1}));            // voice 8
  sysex(values(0, 1, 17, {1}, 2));         // another version
  sysex(values(0, 1, 17, {200}));          // a byte over 127
  idle_all();
  CHECK(umhi::track[0].v[0] == 10 && umhi::track[0].v[15] == 115 && sent(4).empty());
  {
    auto o = block(0);
    CHECK(near(o[5], 2 / 256.0f));   // no more params_hi calls
  }
  printf("test_um_pages: a turn taken (no echo), five refused\n");

  // ---- the stepped knobs as text (the display, the web): P4 the pair "N10 / N10" (entry 180), P5 entry 2, P1 plain
  {
    char b[24];
    CHECK(hooks.user_value_text && hooks.user_value_text(0, 3, b, sizeof b) && std::string(b) == "N10 / N10");
    CHECK(hooks.user_value_text(0, 4, b, sizeof b) && std::string(b) == "2");
    CHECK(!hooks.user_value_text(0, 0, b, sizeof b) && !hooks.user_value_text(0, 16, b, sizeof b));
    report(0, 0, -1, {0, 100 << 7, 100 << 7, (int)rmp_step_v14(361, 20), (int)rmp_step_v14(5, 4)});
    CHECK(hooks.user_value_text(0, 3, b, sizeof b) && std::string(b) == "N2 / N2");
    CHECK(hooks.user_value_text(0, 4, b, sizeof b) && std::string(b) == "4");
    report(0, 0, -1, {0, 100 << 7, 100 << 7, 16383, 0});   // the Rytm's top (127 + 127/128): the last entry
    CHECK(hooks.user_value_text(0, 3, b, sizeof b) && std::string(b) == "N19 / N19");
    auto o = block(0);
    CHECK(near(o[4], 360 / 512.0f) && near(o[7], 0.0f));
  }
  printf("test_um_pages: pair text N10 / N10, N2 / N2, N19 / N19; the plugin's entries the same\n");

  // ---- poly instances of the voice: each fresh state handed the voice's values, a turn reaches them all
  {
    float o[kBlock];
    report(0, 0, -1, view(100));
    CHECK(um::inst_note_on(0, 5, 60.0f, 1.0f) && um::inst_note_on(0, 6, 64.0f, 1.0f));
    um::inst_render(0, 5, o);
    CHECK(near(o[1], 99 / 128.0f) && near(o[5], 1 / 256.0f));
    sysex(values(0, 1, 30, {50}));
    idle_all();
    um::inst_render(0, 5, o);
    CHECK(near(o[1], 50 / 128.0f) && near(o[5], 2 / 256.0f));
    um::inst_render(0, 6, o);
    CHECK(near(o[1], 50 / 128.0f));
    um::inst_stop(0);
  }
  printf("test_um_pages: poly instances: handed at their voice_init, a turn reaches each\n");

  // ---- the kit file: the turns so far went into the project's file (no kit named yet); kit 5 (no file of its own)
  // reads it; a turn there writes kit 5's UMX lines; a kit whose file has no UMX line: the defaults; back: the values
  {
    ins::kit_changed(4);   // kit 5: none of its own, the project's
    idle_all();
    CHECK(umhi::track[0].slot == 0 && umhi::track[0].v[13] == 50 && umhi::track[0].v[1] == def(18));
    sysex(values(0, 1, 18, {33, 44}));
    idle_all();
    g_t += 1100000;
    host_fake_time(true, g_t);
    for (int i = 0; i < 64; ++i) idle_save();
    std::string fn = std::string(tmpl) + "/.rytm/ins/PG/k005.txt", got = file(fn);
    char want[80];
    snprintf(want, sizeof want, "UMX V1 17 1 %04x 10 33 44 1\n", (unsigned)um::slot_view(0).fp);
    CHECK(got.find(want) != std::string::npos && got.find("UMX V1 29 1") != std::string::npos && got.find("UMX V2") == std::string::npos);
    static const char kNone[] = "# Sidecar inserts\n";
    store::put("/.rytm/ins/PG/k007.txt", (const uint8_t*)kNone, sizeof kNone - 1);
    ins::kit_changed(6);   // kit 7: a file without UMX lines - forgotten, the machine's defaults again
    CHECK(umhi::track[0].slot == umhi::kNone);
    idle_all();
    CHECK(umhi::track[0].slot == 0 && umhi::track[0].v[1] == def(18) && umhi::track[0].v[13] == def(30));
    {
      report(0, 0, -1, view(0));
      auto o = block(0);
      CHECK(near(o[1], def(18) / 128.0f));
    }
    // malformed UMX lines (voice 9, P33, slot 6, a bad tag, a bus, P16): lines this build does not read - the file is
    // kept, not written over; the good line of the same file taken
    static const char kBad[] = "UMX V9 17 1 0001 1\nUMX V1 33 1 0001 1\nUMX V1 17 6 0001 1\nUMX V1 17 1 00g1 1\n"
                               "UMX B1 17 1 0001 1\nUMX V1 16 1 0001 1\nUMX V2 25 3 0abc 1 2\n";
    store::put("/.rytm/ins/PG/k008.txt", (const uint8_t*)kBad, sizeof kBad - 1);
    g_log.clear();
    ins::kit_changed(7);
    CHECK(!g_log.empty() && g_log.back().find("1 lines from /.rytm/ins/PG/k008.txt (lines it does not read") != std::string::npos);
    CHECK(umhi::track[1].slot == 2 && umhi::track[1].tag == 0xabc && umhi::track[1].n == 10 && umhi::track[1].v[8] == 1 &&
          umhi::track[1].v[9] == 2 && umhi::track[0].slot == umhi::kNone);
    ins::kit_changed(5);   // kit 6: the project's file again (P30 50, P18 its default)
    idle_all();
    CHECK(umhi::track[0].v[13] == 50 && umhi::track[0].v[1] == def(18));
    ins::kit_changed(4);
    CHECK(umhi::track[0].slot == 0 && umhi::track[0].v[1] == 33 && umhi::track[0].v[2] == 44 && umhi::track[0].n == 16);
    g_msgs.clear();
    idle_all();
    auto o = block(0);
    CHECK(near(o[1], 33 / 128.0f) && near(o[2], 44 / 128.0f));
    CHECK(sent(4).size() == 1 && sent(4)[0][12] == 33);   // the Rytm told the kit's values
    printf("test_um_pages: kit file UMX lines (%s), another kit's defaults, back\n", fn.c_str());
  }

  // ---- the machine changes: voice 0 to U2 (plain API 2, v2_pages): its defaults; the API 2 render shows them
  {
    report(1, 0, 60, view(0));
    CHECK(umhi::track[0].slot == 1 && umhi::track[0].v[1] == def(18) && umhi::track[0].tag == um::slot_view(1).fp);
    auto o = block(0);
    for (int j = 0; j < 12; ++j) CHECK(near(o[j], def(17 + j) / 128.0f));
    sysex(values(0, 1, 18, {5}));   // (U1's: not the voice's machine now)
    sysex(values(0, 2, 18, {6}));
    idle_all();
    o = block(0);
    CHECK(near(o[1], 6 / 128.0f));
    report(1, 0, -1, view(100));
    o = block(0);
    CHECK(near(o[6], 0.25f) && near(o[5], 2 / 256.0f) && near(o[3], 115 / 128.0f));
    // U3 (no knobs_hi, no params_hi): the voice holds none
    report(2, 0, 60, {0, 100 << 7});
    CHECK(umhi::track[0].slot == 2 && umhi::track[0].n == 0);
    g_msgs.clear();
    sysex({0xf0, 0x7d, 0x52, 0x54, 0x52, 0x01, 0x05, 0x00, 0xf7});   // 52 ASK: all pages, every voice's values
    idle_all();
    CHECK(sent(0).size() == 5 && sent(4).empty());
  }
  printf("test_um_pages: a machine change takes its defaults (API 2's render), a machine without P17..P32 none\n");

  // ---- the console, a hello, the 2 s refresh
  {
    report(0, 1, 60, view(0));
    CHECK(cmd("umknob=2:32:77").find("V2 U1 P17..P32") != std::string::npos && umhi::track[1].v[15] == 77);
    CHECK(cmd("umknob=2:33:1").find("umknob=V:P:VAL") == 0 && cmd("umknob=3:17:1").find("umknob=V:P:VAL") == 0);
    CHECK(cmd("umknob=2:17:128").find("umknob=V:P:VAL") == 0);
    g_msgs.clear();
    sysex({0xf0, 0x7d, 0x52, 0x54, 0x35, 0x01, 0x00, 0x00, 0x00, 0xf7});   // a hello
    idle_all();
    CHECK(sent(4).size() == 1 && sent(4)[0][7] == 1 && sent(4)[0][26] == 77);
    g_msgs.clear();
    idle_all();
    CHECK(sent(4).empty());
    g_t += 2100000;
    host_fake_time(true, g_t);
    idle_all();
    CHECK(sent(4).size() == 1);
  }
  printf("test_um_pages: umknob, a hello, the 2 s refresh\n");
  printf("test_um_pages: PASS\n");
  return 0;
}
#else
int main() {
  printf("test_um_pages: skipped (the packages are AArch64: an arm64 host runs them)\n");
  return 0;
}
#endif
