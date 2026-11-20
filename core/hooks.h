// core/hooks.h - the platform layer's entry points into the core: where the core's SysEx for the host goes, and
// the user slots U1..U5 (machine plugins: the Teensy's RytmPlugins glue in voiceloop.ino). A board without
// machine plugins leaves every user hook null: U1..U5 are silent and core/slots.cpp answers the Rytm's slot
// messages as EMPTY slots.
#pragma once
#include <stdint.h>

namespace core {

struct Hooks {
  void (*to_rytm)(const uint8_t* m, int n);   // a SysEx F0..F7 for the host; null: dropped
  void (*trig_seen)();                        // a 0x04 trig report arrived (the Teensy's transport guess)
  // ---- user slots (all null without machine plugins)
  // A report for voice vi on user slot `slot` (0..4); was_slot the voice's slot before (-1 none). v[vi].p holds
  // its P1..P16; note -1 for a 0x03; tag the sound tag (0 untagged).
  void (*user_report)(int vi, int slot, int was_slot, int note, int vel, int tag);
  void (*user_leave)(int vi);                 // voice vi plays a built-in machine now
  void (*user_note_off)(int vi);              // 0x11 for a voice on a user slot
  void (*user_render)(int vi, float* out);    // kBlock samples, -1..1
  // Plugin API 3: a voice on OUT PRE / POST (a stereo pair): L into l, and R into r when its plugin renders stereo
  // (true); false: l only (a mono plugin: R = L, as before). Null: user_render for every voice.
  bool (*user_render2)(int vi, float* l, float* r);
  // voice vi's P(k+1) as its user slot's manifest labels it now (the short label, the set a list knob picks): into b,
  // "" when that knob has none (unused); null hook or a false return: no plugin on the voice (core/ui voice_label)
  bool (*user_label)(int vi, int k, char* b, int cap);
  // voice vi's P(k+1) as text when its machine's manifest makes it a stepped knob (0.5.0-pages: a pair's "B3 / B12", a
  // number's entry); false: a plain knob (show its number), or no plugin. Any core (the display, the web).
  bool (*user_value_text)(int vi, int k, char* b, int cap);
  // Plugin-drawn screens (docs/um-pages.md; sdk/rmp/rmp_api.h RMP_P_DRAW), the control core, null without plugins.
  // user_draw_key: nonzero while user slot `slot` (0..4) is READY, not muted and its plugin has a draw; it changes when
  // anything the draw gets besides P1..P16 / page / flags / track changes (the plugin, voice vi's P17..P32, whether a
  // voice plays it). 0: no draw (the host's default). user_draw: the plugin's draw for track t's view `page` (RMP_PAGE_*),
  // p16 = P1..P16 as the page shows them, voice vi's P17..P32 added by the hook, into fb (RMP_DRAW_BYTES, row-major,
  // cleared by the hook), guarded (a fault mutes the slot); 1 drawn, 0 not (declined, faulted, none).
  uint32_t (*user_draw_key)(int vi, int slot);
  int (*user_draw)(int vi, int slot, int track, int page, uint32_t flags, const float* p16, uint8_t* fb);
  // An effect's pages (sendfx-draw: the Rytm's SEND FX pages of send slot sl 0..1, RMP_PAGE_FX1..FX3). fx_draw_key:
  // nonzero while sl plays a READY, unmuted user effect whose plugin has a draw; it changes with the plugin (a reload).
  // fx_draw_values: p[0..17] = the effect's P1..P16 as its render gets them, TYPE, ROUTE (the caller adds BASE / WIDTH).
  // fx_draw: the draw with p[0..19], into fb (RMP_DRAW_BYTES, row-major, cleared), guarded; 1 drawn, 0 not.
  uint32_t (*fx_draw_key)(int sl);
  void (*fx_draw_values)(int sl, float* p18);
  int (*fx_draw)(int sl, int page, const float* p20, uint8_t* fb);
  void (*slot_ask)(const uint8_t* d, int len);      // 0x19 (null: core/slots.cpp)
  void (*slot_pages)(const uint8_t* d, int len);    // 0x52 UM PAGES from the Rytm (0.5.0-pages; null: ignored)
  void (*slot_hello)();                       // 0x35: the Rytm's USB port came up (null: ignored)
  void (*slot_assign)(const uint8_t* d, int len);   // 0x1E (null: core/slots.cpp)
  void (*project_seen)();                     // any 0x10 arrived (null: core/slots.cpp's rate-limited 1D)
  void (*project_changed)();                  // 0x10 named another project
  void (*slot_prints)(const uint8_t* d, int len);   // 0x25 SLOT PRINTS: the project's sounds' fingerprints per slot (null: ignored)
  void (*pool_changed)(int line);             // pool line `line` (0..126) changed; -1 the whole pool
  // the Rytm's transport as the mods report it (seq's kTransport state, whatever the Rytm's MIDI TRANSPORT SEND says),
  // as MIDI realtime: 0xFA start, 0xFB continue, 0xFC stop (pause too). The control core; null: ignored. The Pi's
  // recorder arms on it as on the MIDI messages (its gate is idempotent: both may arrive).
  void (*transport)(uint8_t status);
  // Another box needs another state folder (core/device/device.h): saved, restart the Sidecar. Null: a board that cannot.
  void (*device_restart)();
  // The recent log copied to the card once, as /.rytm/logs/<name>.log (the Rytm's first freeze probe of a boot,
  // core/host_link.cpp). Any core; only asks: the platform writes it later, off the audio path. Null: nothing kept.
  void (*log_snapshot)(const char* name);
};
extern Hooks hooks;
// True while the core itself calls user_report for a parameter change of its own (voice_params_changed: the Sidecar's
// LFOs, the sequencer's step locks), not for a report from the Rytm: a user machine's glue tells a knob the user moved
// from a modulation (core/user/um.cpp: whose TYPE a slot's labels follow).
extern bool user_report_internal;

uint32_t to_rytm_dropped();                  // messages to_rytm dropped (no hook)
void to_rytm(const uint8_t* m, int n);

}  // namespace core
