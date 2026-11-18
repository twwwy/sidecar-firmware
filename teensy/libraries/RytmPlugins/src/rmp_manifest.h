// rmp_manifest.h - a package's manifest (formats 1..5; 4 is 3 with an RLB2 block, 5 one with a list over 16 names; any
// of them may carry the pages block RPG1) as a slot description, read through a
// streaming callback (the store's read_at: never the package whole in RAM). The Teensy serves
// 1A (metadata) and 1B (enum names) from it (sim/protocol.py). Portable: the Mac tests it.
#pragma once
#include <stdint.h>
#include "rmp_loader.h"
#include "rmp_format.h"
#include "rmp_api.h"   // rmp_step_v14 / rmp_step_entry, RMP_STEPS_MAX
#define RMP_DESC_KNOBS 16
#define RMP_STEP_TEXT_BYTES 18   // rmp_step_pair_text: "1234567 / 1234567" and the NUL
struct rmp_desc_knob {
  uint8_t p;                          // 1..16
  uint8_t look, kind, role;           // RMP_LOOK_*, RMP_TEXT_* (the manifest's "text"), RMP_ROLE_*
  uint8_t min, max, def;              // 0..127, min <= def <= max
  uint8_t enum_first, enum_count;     // into the package's name table (enum_count <= RMP_ENUM_MAX)
  char long_name[13], short_name[5];  // printable ASCII, NUL-padded and always NUL-terminated (12 / 4 characters)
};
struct rmp_desc {
  char name[11], short_name[5], p2_title[14];   // printable ASCII, NUL-padded and always NUL-terminated
                                                // (10 / 4 / 13 characters: the manifest's widths, plus the NUL)
  char preset_ext[8];                           // NUL-terminated, letters and digits; "" none
  uint8_t page2;                                // 1: P9..P16 are knobs ("params"), 0: the sample layer
  uint8_t nknobs;                               // knob[0..nknobs-1] in p order, each p once
  int8_t pre;                                   // the knob with kind preset (the first by p), -1 none
  uint8_t format;                               // the package's format, 1..5
  uint32_t names_at, names_end;                 // the name table's package offsets (0, 0: none)
  rmp_desc_knob knob[RMP_DESC_KNOBS];
};
// The manifest of a package whose header *info came from rmp_verify_header (format, image_size,
// reloc_count, manifest_size): false on a read error, a format outside 1..4, more than 32 knobs, or a
// manifest shorter than its head and knobs. Knobs with p outside 1..16, or a p seen before, are skipped;
// strings become printable ASCII ('_' for others), NUL-padded and NUL-terminated (every string field
// has one byte more than its manifest width). Format-3 min/max/default bytes above 127 saturate to
// 127; a range with min > max is read as 0..127 with the default 64; otherwise the default is clamped
// into min..max. Reads: the head, then one knob a read.
bool rmp_manifest_read(const rmp_info *info, rmp_read_fn rd, void *ctx, rmp_desc *d);
// Knob k's enum names (knob[k].enum_count of them) into names[][8], NUL-padded, at most 7 characters:
// the count, 0 when it has none, -1 on a read error or a name table that does not hold them.
int rmp_manifest_enum(const rmp_desc *d, int k, rmp_read_fn rd, void *ctx, char names[RMP_ENUM_MAX][8]);
// The knob for P p (1..16): its index in knob[], -1 none.
int rmp_desc_find(const rmp_desc *d, int p);
// Labels that follow a list knob (rmp_format.h RMP_LABELS_*): per value of the list knob `knob`, the long / short
// label of each P in p[]. nsets 0: the package has none (or a malformed block: ignored, the static labels stand).
// dep (an RLB2 block): the list knob whose value picks knob's names - count[v] of them for dep value v, names[base[v]..]
// - and the set: base[v] + knob's zone among count[v]. dep 0: an RLB1 block (one set per name of knob, none of these).
struct rmp_labels {
  uint8_t knob, nsets, nparams;                                       // knob: the list knob's P (1..16)
  uint8_t dep, ndep;                                                  // RLB2: dep's P and names count (0, 0: RLB1)
  uint8_t count[RMP_LABELS_DEPS], base[RMP_LABELS_DEPS];              // knob's names for each value of dep
  uint8_t p[RMP_LABELS_PARAMS];                                       // the P each column labels
  char names[RMP_LABELS_SETS][RMP_ENUM_NAME_BYTES];                   // RLB2: knob's names, set order (NUL-padded, 7 at most)
  char lbl[RMP_LABELS_SETS][RMP_LABELS_PARAMS][RMP_LABEL_BYTES];      // [set][column]: long 12, short 4, NUL-padded
};
// The block at the end of d's manifest (d from rmp_manifest_read of the same package): true with *out filled, false
// (out->nsets 0) when there is none, the read failed, or it does not fit d - its knob not a list knob of d with nsets
// names (RLB1; RLB2: a list knob, dep a list knob of d with ndep names, the counts adding up to nsets), a P outside
// 1..16, twice, the list knob itself (or dep), or not one of d's knobs. Two reads (the footer, the block, into a static
// buffer: not reentrant; the control core's).
bool rmp_manifest_labels(const rmp_info *info, rmp_read_fn rd, void *ctx, const rmp_desc *d, rmp_labels *out);
// The set a value of the list knob means (0..nsets-1): (value * nsets) >> 7 of value 0..127 (NaN: 0); -1 none (or an
// RLB2 block, whose set needs dep's value too: rmp_labels_pick).
int rmp_labels_set(const rmp_labels *l, float value);
// The set the knob values p[0..15] (P1..P16) mean: RLB1 as rmp_labels_set(p[knob - 1]); RLB2 dep's zone v among ndep,
// then base[v] + knob's zone among count[v]. -1 none.
int rmp_labels_pick(const rmp_labels *l, const float *p);
// d's knobs in l->p[] take set's labels (an all-NUL entry: "" - the Rytm greys an unlabelled knob); RLB2: knob's
// enum_count becomes count[v] for the dep value v of set (1A's enum count: the zones the Rytm shows). False: nothing.
bool rmp_labels_apply(const rmp_labels *l, int set, rmp_desc *d);
// RLB2: knob's names for set's dep value (count[v] of them, into names[][8], NUL-padded) when p is knob; -1 otherwise
// (RLB1, another P, no set: the package's own name table, rmp_manifest_enum).
int rmp_labels_enum(const rmp_labels *l, int set, int p, char names[RMP_ENUM_MAX][8]);
// Pages 3 and 4, and stepped knobs (rmp_format.h RMP_PAGES_*, the "RPG1" block; 0.5.0-pages): the knobs P17..P32 the
// host holds and gives the plugin through params_hi (rmp_api.h RMP_P_PARAMS_HI), the two pages' titles, and the P1..P16
// knobs whose 14-bit value picks one of `steps` entries (rmp_api.h rmp_step_v14 / rmp_step_entry). present 0: the
// package has none (or a malformed block: ignored; no P17..P32, every knob plain 7-bit).
struct rmp_step_desc {
  uint8_t p;                          // 1..16, one of the description's knobs
  uint8_t kind;                       // RMP_STEP_NUMBER (shown as the entry 0..steps-1) or RMP_STEP_PAIR
  uint16_t steps;                     // 2..RMP_STEPS_MAX; RMP_STEP_PAIR: name_count * name_count
  uint16_t def14;                     // the default as a 14-bit value, 0..RMP_STEP_V14_MAX
  uint8_t name_first, name_count;     // RMP_STEP_PAIR: n names (2..RMP_ENUM_MAX) of the block's table; else 0, 0
};
struct rmp_pages {
  uint8_t present;                    // 1: the package has the block
  uint8_t nhi, nstep;                 // hi[0..nhi-1] in p order (17..32, each once); step[0..nstep-1] in p order
  char p3_title[14], p4_title[14];    // printable ASCII, NUL-padded and NUL-terminated (13 characters); "" none
  rmp_desc_knob hi[RMP_PAGES_KNOBS];  // as rmp_manifest_read's knobs (p 17..32, role free, kind never preset;
                                      // enum_first / enum_count index the block's own name table)
  rmp_step_desc step[RMP_PAGES_STEPS];
  uint32_t names_at;                  // the block's name table's package offset
  uint16_t nnames;                    // its entries (RMP_ENUM_NAME_BYTES each)
};
// The block of d's manifest (d from rmp_manifest_read of the same package), found by walking the appended blocks'
// footers back from the manifest's end through the known magics (RLB1, RLB2, RPG1; rmp_format.h): true with *out
// filled, false (out all zero) when there is none, a read failed, or the block is malformed (a length or count that
// does not add up, past the name table, more than 16 hi knobs or steps). Entries that do not fit are skipped as
// rmp_manifest_read skips knobs: a hi knob with p outside 17..32 or seen before (its role read as free, a preset
// kind as number, names outside the table as none, the range as rmp_manifest_read's); a step whose p is not one of
// d's knobs or seen before, steps outside 2..RMP_STEPS_MAX, a kind other than the two, a pair whose names are not
// 2..RMP_ENUM_MAX inside the table or whose steps are not n * n, a number with names (def14 above RMP_STEP_V14_MAX is
// read as RMP_STEP_V14_MAX). Reads: the footers, the head, then one knob or step a read (no buffer: not static).
bool rmp_manifest_pages(const rmp_info *info, rmp_read_fn rd, void *ctx, const rmp_desc *d, rmp_pages *out);
// The hi knob for P p (17..32): its index in hi[], -1 none.
int rmp_pages_find(const rmp_pages *pg, int p);
// The stepped knob P p (1..16): its step entry, NULL when P p is a plain knob.
const rmp_step_desc *rmp_pages_step(const rmp_pages *pg, int p);
// Hi knob k's enum names (hi[k].enum_count of them) into names[][8], NUL-padded, at most 7 characters: the count, 0
// when it has none, -1 on a read error.
int rmp_pages_enum(const rmp_pages *pg, int k, rmp_read_fn rd, void *ctx, char names[RMP_ENUM_MAX][8]);
// A pair knob's names (s->name_count of them) into names[][8], as rmp_pages_enum: the count, 0 for an RMP_STEP_NUMBER
// knob, -1 on a read error.
int rmp_pages_pair_names(const rmp_pages *pg, const rmp_step_desc *s, rmp_read_fn rd, void *ctx,
                         char names[RMP_ENUM_MAX][8]);
// Entry q of a pair knob as text: names[q / n] " / " names[q % n] (n names; q clamped to 0..n*n-1) into out
// (NUL-terminated, 7 + 3 + 7 characters at most): its length, 0 with out "" when n < 1.
int rmp_step_pair_text(const char names[][8], int n, uint32_t q, char out[RMP_STEP_TEXT_BYTES]);
// A description without a manifest (a missing package): the name is the id's last dot-separated
// part in upper case, at most 10 characters (befaco.np.atari -> "ATARI"), the short name its first 4
// (both NUL-terminated); no knobs.
void rmp_desc_from_id(const char *id, rmp_desc *d);
