// core/um_hi.h - a user machine's P17..P32 (0.5.0-pages, docs/um-pages.md): SRC page 2 (P17..P24) and SMPL page 2
// (P25..P32), values 0..127 the Sidecar holds per voice V1..V8 (never in the Rytm's kit: its sound words are P1..P16,
// so they cannot be p-locked), turned on the Rytm (52 UM PAGES VALUES, rmp_proto.h), handed to the plugin by params_hi
// (sdk/rmp/rmp_api.h RMP_P_PARAMS_HI) and saved in the kit's INSERT file (UMX lines, core/fx/insert_card.cpp).
// slot / tag: the user slot (0..4) and its plugin's fingerprint (rmp_plugin_fp, the sound tag) the values belong to; a
// voice whose machine is another starts from that machine's manifest defaults (um.cpp hi_follow). 0xFF: none yet (a kit
// load without UMX lines for the voice). Control core only. Header only: insert.cpp and insert_card.cpp also link
// without um.cpp (as fx::xknobs_load in fx/send_fx.h), and outside core/user/: the Teensy's core library has inserts
// but no core/user (teensy/setup_core_lib.sh).
#pragma once
#include <stdint.h>

#include "machines.h"

namespace core {
namespace umhi {

constexpr int kHi = 16;          // P17..P32
constexpr int kFirstP = 17;
constexpr uint8_t kNone = 0xFF;
struct Track {
  uint8_t v[kHi];                // P17..P32, 0..127
  uint8_t slot;                  // 0..4, kNone
  uint16_t tag;                  // the slot's plugin fingerprint when the values were taken
  uint8_t n;                     // P17..P(16 + n) are the machine's (the highest P17..P32 its manifest names - 16; a file's: 4 a line)
};
inline Track track[kVoices] = {{{0}, kNone, 0, 0}, {{0}, kNone, 0, 0}, {{0}, kNone, 0, 0}, {{0}, kNone, 0, 0},
                               {{0}, kNone, 0, 0}, {{0}, kNone, 0, 0}, {{0}, kNone, 0, 0}, {{0}, kNone, 0, 0}};
inline uint32_t gen[kVoices];    // bumped at each change of track[vi]: um.cpp hands the states the new values
inline void (*changed)() = nullptr;   // the INSERT file's dirty mark (insert.cpp sets it; null without inserts)

// From the kit's INSERT file (a UMX line): voice vi's values for slot / tag, P(first).. (n of them, -1 kept).
inline void load(int vi, int slot, uint16_t tag, int first, const int* v, int n) {
  if (vi < 0 || vi >= kVoices || slot < 0 || slot > 4 || first < kFirstP || first > kFirstP + kHi - 1) return;
  Track& t = track[vi];
  if (t.slot != slot || t.tag != tag) {   // the first line for this machine: the rest 0
    for (uint8_t& x : t.v) x = 0;
    t.slot = (uint8_t)slot, t.tag = tag, t.n = 0;
  }
  int j0 = first - kFirstP;
  for (int j = 0; j < n && j0 + j < kHi; ++j)
    if (v[j] >= 0) t.v[j0 + j] = (uint8_t)(v[j] > 127 ? 127 : v[j]);
  int top = j0 + n > kHi ? kHi : j0 + n;
  if (top > t.n) t.n = (uint8_t)top;
  ++gen[vi];
}
// Before a kit's file is read: none (each voice takes its machine's defaults again, unless UMX lines say otherwise).
inline void forget() {
  for (int vi = 0; vi < kVoices; ++vi) {
    Track& t = track[vi];
    for (uint8_t& x : t.v) x = 0;
    t.slot = kNone, t.tag = 0, t.n = 0;
    ++gen[vi];
  }
}

}  // namespace umhi
}  // namespace core
