// rmp_api.h - the ABI between the Teensy's voiceloop (the host) and a machine
// plugin (.rmp). Version RMP_API 2 (API 1 plugins still load: every API 2
// addition sits in a reserved slot, sizes unchanged). Shared by the Teensy, the
// Mac harness and plugins. Everything a plugin may call is in rmp_host;
// everything the host calls is in rmp_plugin. Plain C, fixed-size types, no
// host symbols.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define RMP_API 2u
#define RMP_API_MIN 1u                 /* the oldest plugin API the host loads */
#define RMP_API_RMPA 3u                /* the newest API an RMPA (AArch64, the Pi) package may have: API 3 (rmp_api3.h);
                                          a plugin built against this header still writes .api = 2u (RMP_API) */
#define RMP_PLUGIN_MAGIC 0x504D5252u   /* "RRMP" little-endian */
#define RMP_PARAMS 16                  /* P1..P16: SRC page P1..P8, SMPL page P9..P16 */
#define RMP_PARAMS_HI 16               /* P17..P32 (params_hi): SRC's second page P17..P24, SMPL's P25..P32 */

/* A stepped knob (the manifest's "steps", docs/plugins.md section 3): a P1..P16 knob whose 14-bit value
   v14 = value * 128 + fraction (0..RMP_STEP_V14_MAX: 0.0 .. 127.0; the Rytm's 127 + 127/128 = 16383 reads as the
   last entry) picks one of `steps` entries (2..RMP_STEPS_MAX), one entry per Rytm detent. Defined once, here, for
   the packer (tools/rmp_pack.py step_v14 / step_entry), the hosts and plugins alike; integer, rounding:
     v14(q) = (q * 16256 + (steps - 1) / 2) / (steps - 1)      entry q's value (0 -> 0, steps - 1 -> 16256)
     q(v14) = min((v14 * (steps - 1) + 8128) / 16256, steps - 1)
   Every entry round-trips (q(v14(q)) == q: an entry is at least one 14-bit step wide, so rounding there and back
   errs by under half an entry), and q(v) is monotonic in v. The products stay under 2^31 (16383 x 16255). */
#define RMP_STEP_V14_MAX 16256u        /* 127.0 in 1/128 */
#define RMP_STEPS_MAX 16256u           /* entries at most: one per 14-bit step of 0..127.0 */
/* Entry q's 14-bit value (q above steps - 1: the last entry's; steps under 2: 0). */
static inline uint32_t rmp_step_v14(uint32_t steps, uint32_t q) {
  if (steps < 2u) return 0u;
  if (steps > RMP_STEPS_MAX) steps = RMP_STEPS_MAX;
  if (q > steps - 1u) q = steps - 1u;
  return (q * RMP_STEP_V14_MAX + (steps - 1u) / 2u) / (steps - 1u);
}
/* The entry a 14-bit value means (0..steps - 1; values above RMP_STEP_V14_MAX are the last entry). */
static inline uint32_t rmp_step_entry(uint32_t steps, uint32_t v14) {
  if (steps < 2u) return 0u;
  if (steps > RMP_STEPS_MAX) steps = RMP_STEPS_MAX;
  if (v14 > 16383u) v14 = 16383u;
  uint32_t q = (v14 * (steps - 1u) + RMP_STEP_V14_MAX / 2u) / RMP_STEP_V14_MAX;
  return q > steps - 1u ? steps - 1u : q;
}
/* The entry a params() value p[k] (value + fraction / 128) means: v14 = p * 128 (exact for such values; rounded to
   nearest otherwise), NaN and negatives entry 0. A plugin reads its stepped knob with this. */
static inline uint32_t rmp_step_entry_of(uint32_t steps, float value) {
  float v = value * 128.0f + 0.5f;
  uint32_t v14 = v >= 1.0f ? (v < 16383.0f ? (uint32_t)v : 16383u) : 0u;   /* (NaN fails v >= 1: 0) */
  return rmp_step_entry(steps, v14);
}

/* API 2: a pool sample, as pool_info reports it. */
typedef struct rmp_pool_info {
  uint32_t frames;       /* frames in the sample */
  uint32_t channels;     /* stored channels pool_read delivers interleaved (1 or 2) */
  float rate;            /* its sample rate */
  uint32_t where;        /* RMP_POOL_PSRAM (all of it, random access) or RMP_POOL_HEAD */
  uint32_t head_frames;  /* RMP_POOL_HEAD: frames 0..head_frames-1 are random access, the rest
                            streams in order (a jump outside head + ring restarts the stream) */
} rmp_pool_info;
enum { RMP_POOL_PSRAM = 1, RMP_POOL_HEAD = 2 };
/* pool_info's result */
enum { RMP_POOL_OK = 0, RMP_POOL_NOT_READY = 1, RMP_POOL_EMPTY = 2 };

typedef struct rmp_host {
  uint32_t api;                 /* RMP_API, or 1 for an API-1 plugin (which never sees reserved[]) */
  float sample_rate;            /* 48000 */
  int32_t max_block;            /* the largest n render() is called with (12 today) */
  void (*log)(const char *msg); /* a line in the Teensy's log, not from render() */
  uint32_t (*random)(uint32_t *state);   /* xorshift32 on the caller's state */
  /* API 2: [RMP_H_POOL_INFO] int32_t pool_info(int32_t slot, rmp_pool_info *info):
       pool slot 1..127 of the project; RMP_POOL_OK with *info, RMP_POOL_NOT_READY while the host
       prepares it (asked for a slot this voice does not stream: a job between render requests),
       RMP_POOL_EMPTY when there is no such sample. Call it about once a block (render, note_on), not
       per sample: the Teensy runs it from flash. Asked in voice_init, its prepare starts once the voice
       plays the slot; in voice_leave it answers RMP_POOL_EMPTY.
     [RMP_H_POOL_READ] int32_t pool_read(int32_t slot, uint32_t pos, float *buf, int32_t n):
       n frames from frame pos, the stored channels interleaved, -1..1; returns the frames
       delivered (fewer when not there yet: play silence for the rest). For the voice the host
       is calling. Callable from render().
     [2] API 3 (rmp_api3.h RMP_H_V3): const rmp3_host *, NULL on an API 2 host.
     [3..10] later APIs, NULL. */
  void *reserved[11];
} rmp_host;
#define RMP_H_POOL_INFO 0
#define RMP_H_POOL_READ 1

typedef struct rmp_plugin {
  uint32_t magic;               /* RMP_PLUGIN_MAGIC */
  uint32_t api;                 /* RMP_API the plugin was built for (1 or 2) */
  uint32_t state_bytes;         /* per voice, 8-byte aligned by the host, zeroed before voice_init */
  uint32_t cpu_permille;        /* declared CPU per voice in 1/1000 of the Teensy (guard budget) */
  /* once after loading; nonzero = refuse (the plugin is unloaded) */
  int32_t (*init)(const rmp_host *host);
  /* a voice starts using this machine (state zeroed) */
  void (*voice_init)(void *state);
  /* a trig: note in semitones (60 = C4), velocity 0..1 */
  void (*note_on)(void *state, float note, float velocity);
  /* the voice's gate closed (Rytm 0x11) */
  void (*note_off)(void *state);
  /* P1..P16, each 0..127 with the 7-bit fraction (value + fraction/128) */
  void (*params)(void *state, const float *p);
  /* n samples (1..max_block), mono, written (not added) to out, nominal level +-1 */
  void (*render)(void *state, float *out, int32_t n);
  /* API 2 (an API-1 plugin's are NULL and never read; a NULL one turns the feature off):
     [RMP_P_PARSE]       banks: later, NULL now
     [RMP_P_LOAD_ENTRY]  int32_t load_entry(void *state, const uint8_t *bytes, int32_t len, int16_t values[16]):
                         a native preset file: set this voice's hidden state and fill values[k] (P(k+1)'s
                         0..127) for the preset-role knobs, -1 = untouched; between render calls, guarded
     [RMP_P_VOICE_ENTER] void voice_enter(void *state): after voice_init, each time a voice starts this slot
     [RMP_P_VOICE_LEAVE] void voice_leave(void *state): before the voice leaves this slot or it unloads
     [RMP_P_V3 = 4] API 3 (rmp_api3.h): const rmp3_plugin *, NULL = a plain API 2 plugin
     [RMP_P_PARAMS_HI = 5] void params_hi(void *state, const float *p): p[0..15] = P17..P32 (RMP_PARAMS_HI), the same
                         convention as params() (0..127 + fraction/128; the host holds them per track: they are no
                         Rytm sound words and not p-lockable). Called like params(): on the control core between
                         render calls, all 16 values, at least once after voice_init (and voice_enter) before the
                         voice renders, then whenever one changes; every state of the track (its poly instances, a
                         unison stack) gets them. The manifest's "knobs_hi" describes them (docs/plugins.md section 3;
                         a P without a knob gets 0). An older host never calls it: start P17..P32 at the manifest's
                         defaults in voice_init. API 2 and API 3 machines alike (an API 3 machine also learns it
                         from rmp3_host.caps RMP3_CAP_UM_P32). Effects: not called (their P5..P16 are
                         RMP3_CAP_FX_P16's). (RMP_API stays 2: the slot was reserved and NULL in every table, a NULL
                         one means "no P17..P32", and a host from before it never reads reserved[5], so a plugin that
                         fills it loads and plays everywhere; a new number would make older hosts refuse it.
                         reserved[4] is RMP_P_V3's, so params_hi is reserved[5].)
     [RMP_P_DRAW = 6] int32_t draw(void *state, int32_t page, uint32_t flags, int32_t track, const float *p,
                         uint8_t *fb) (rmp_draw_fn below): the machine draws its own Rytm screen (docs/um-pages.md
                         "Plugin-drawn screens"); NULL: the host's default. Same rules for older hosts as params_hi
                         (RMP_API stays 2; an older host never reads reserved[6]).
     [7] later APIs, NULL */
  void *reserved[8];
} rmp_plugin;
#define RMP_P_PARSE 0
#define RMP_P_LOAD_ENTRY 1
#define RMP_P_VOICE_ENTER 2
#define RMP_P_VOICE_LEAVE 3
#define RMP_P_PARAMS_HI 5
/* rmp_plugin.reserved[RMP_P_DRAW]: the machine draws its own Rytm screen (NULL: the host's default) */
#define RMP_P_DRAW 6
#define RMP_DRAW_W 128
#define RMP_DRAW_H 64
#define RMP_DRAW_BYTES 1024              /* 1 bit a pixel, row-major, 16 bytes a row, MSB = the leftmost pixel, 1 = lit */
enum { RMP_PAGE_SRC = 1, RMP_PAGE_SMPL = 2, RMP_PAGE_SRC2 = 3, RMP_PAGE_SMPL2 = 4,
       /* an effect (rmp_api3.h RMP3_KIND_EFFECT, RMP3_CAP_FX_DRAW): the Rytm's SEND FX page 1 (A TYPE, B ROUTE, C BASE,
          D WIDTH, E..H P1..P4), page 2/3 (A..H P5..P12), page 3/3 (A..D P13..P16) */
       RMP_PAGE_FX1 = 5, RMP_PAGE_FX2 = 6, RMP_PAGE_FX3 = 7 };
enum { RMP_DRAW_LOCKED = 1u << 0 };      /* flags: P1..P16 include a held step's locks */
/* an effect's draw: p[] indexes of the send slot's own controls (the host's, 0..127), after the effect's P1..P16 */
enum { RMP_DRAW_FX_TYPE = 16, RMP_DRAW_FX_ROUTE = 17, RMP_DRAW_FX_BASE = 18, RMP_DRAW_FX_WIDTH = 19 };

/* A function in a reserved slot of a table: RMP_FN(load_entry). */
#define RMP_FN(f) ((void *)(f))
typedef int32_t (*rmp_load_entry_fn)(void *state, const uint8_t *bytes, int32_t len, int16_t values[16]);
typedef void (*rmp_voice_fn)(void *state);
typedef void (*rmp_params_fn)(void *state, const float *p);
/* page: RMP_PAGE_*; p[0..31] = P1..P32 as the page shows them (value + fraction/128; a P with no knob 0);
   track 0..11; fb: RMP_DRAW_BYTES, cleared by the host. Return 1 = drawn, 0 = not handled (the host draws its
   default for that page). Control core, between render calls, guarded, keep it under 1 ms. state is the
   track's first voice state, or NULL when no voice plays the machine yet - a draw must not need it.
   Pixel (x, y) from the top-left is lit when fb[y * 16 + x / 8] & (0x80 >> (x % 8)). Pages: RMP_PAGE_SRC the
   Rytm's SRC page (P1..P8 on its encoders), RMP_PAGE_SMPL its SMPL page (P9..P16), RMP_PAGE_SRC2 / RMP_PAGE_SMPL2
   the second SRC / SMPL page (P17..P24 / P25..P32, the manifest's knobs_hi); every page gets all 32 values.
   An effect (RMP3_CAP_FX_DRAW): page RMP_PAGE_FX1..FX3, track = the send slot (0 SEND FX 1, 1 SEND FX 2), flags 0,
   p[0..15] = its P1..P16 as its render gets them, p[16..19] = the slot's TYPE (the send FX id), ROUTE (0..63 PRE,
   64..127 POST), BASE and WIDTH (RMP_DRAW_FX_*), the rest 0; state is always NULL (an effect renders on another core:
   its draw must work from p alone). */
typedef int32_t (*rmp_draw_fn)(void *state, int32_t page, uint32_t flags, int32_t track, const float *p, uint8_t *fb);
typedef int32_t (*rmp_pool_info_fn)(int32_t slot, rmp_pool_info *info);
typedef int32_t (*rmp_pool_read_fn)(int32_t slot, uint32_t pos, float *buf, int32_t n);
/* The host reads a plugin's API-2 functions only through these (NULL for an API-1 plugin). */
static inline rmp_load_entry_fn rmp_load_entry_of(const rmp_plugin *p) {
  return p->api >= 2u ? (rmp_load_entry_fn)p->reserved[RMP_P_LOAD_ENTRY] : (rmp_load_entry_fn)0;
}
static inline rmp_voice_fn rmp_voice_enter_of(const rmp_plugin *p) {
  return p->api >= 2u ? (rmp_voice_fn)p->reserved[RMP_P_VOICE_ENTER] : (rmp_voice_fn)0;
}
static inline rmp_voice_fn rmp_voice_leave_of(const rmp_plugin *p) {
  return p->api >= 2u ? (rmp_voice_fn)p->reserved[RMP_P_VOICE_LEAVE] : (rmp_voice_fn)0;
}
static inline rmp_params_fn rmp_params_hi_of(const rmp_plugin *p) {
  return p->api >= 2u ? (rmp_params_fn)p->reserved[RMP_P_PARAMS_HI] : (rmp_params_fn)0;
}
static inline rmp_draw_fn rmp_draw_of(const rmp_plugin *p) {   /* NULL for api < 2 or a NULL slot */
  return p->api >= 2u ? (rmp_draw_fn)p->reserved[RMP_P_DRAW] : (rmp_draw_fn)0;
}
/* A plugin reads the host's API-2 functions only through these (NULL from an API-1 host table). */
static inline rmp_pool_info_fn rmp_pool_info_of(const rmp_host *h) {
  return h->api >= 2u ? (rmp_pool_info_fn)h->reserved[RMP_H_POOL_INFO] : (rmp_pool_info_fn)0;
}
static inline rmp_pool_read_fn rmp_pool_read_of(const rmp_host *h) {
  return h->api >= 2u ? (rmp_pool_read_fn)h->reserved[RMP_H_POOL_READ] : (rmp_pool_read_fn)0;
}

/* sizeof(rmp_plugin) on the 32-bit Teensy: 4 u32 + 6 function pointers + 8 reserved pointers. The loader
   and tools/rmp_pack.py use this fixed number (a 64-bit Mac build has bigger pointers), so the ARM
   build proves it. */
#define RMP_PLUGIN_BYTES 72u
/* sizeof(rmp_plugin) built for AArch64 (the Pi, RMPA packages; also the arm64 Mac): 4 u32 + 14 pointers. */
#define RMP_PLUGIN_BYTES_A64 128u
#ifdef __arm__
#ifdef __cplusplus
static_assert(sizeof(rmp_plugin) == RMP_PLUGIN_BYTES, "rmp_plugin size (RMP_PLUGIN_BYTES, tools/rmp_pack.py)");
#else
_Static_assert(sizeof(rmp_plugin) == RMP_PLUGIN_BYTES, "rmp_plugin size (RMP_PLUGIN_BYTES, tools/rmp_pack.py)");
#endif
#endif
#ifdef __aarch64__
#ifdef __cplusplus
static_assert(sizeof(rmp_plugin) == RMP_PLUGIN_BYTES_A64, "rmp_plugin size on AArch64 (RMP_PLUGIN_BYTES_A64)");
#else
_Static_assert(sizeof(rmp_plugin) == RMP_PLUGIN_BYTES_A64, "rmp_plugin size on AArch64 (RMP_PLUGIN_BYTES_A64)");
#endif
#endif

/* A plugin exports exactly one of these, named rmp_entry. */
#ifdef __cplusplus
#define RMP_LINKAGE extern "C"   /* so a .cpp plugin's const rmp_entry is a global, unmangled symbol */
#else
#define RMP_LINKAGE
#endif
#ifdef RMP_NATIVE   /* the native build (a .dylib/.so): no ELF section, just exported */
#define RMP_ENTRY RMP_LINKAGE __attribute__((used, visibility("default")))
#else
#define RMP_ENTRY RMP_LINKAGE __attribute__((used, section(".rmp_entry")))
#endif

#ifdef __cplusplus
}
#endif
