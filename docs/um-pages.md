# User machine pages: P17..P32 and stepped knobs (0.5.0-pages)

A user machine (a plugin in U1..U5, docs/plugins.md) had P1..P16: the Rytm's sound words of its SRC page (P1..P8) and
SMPL page (P9..P16), p-lockable. This adds, for any machine whose manifest asks for them:

- **P17..P32**: a second SRC page (P17..P24, encoders A..H) and a second SMPL page (P25..P32). Their values are the
  **Sidecar's**, per voice V1..V8 and saved with the kit on the Sidecar's card; they are no sound words, so they
  **cannot be p-locked** and the Rytm's kit is unchanged. The design is issue #15's (a user effect's P5..P16 on SEND FX
  pages 2 / 3, docs/plugins.md 12b): the host holds the values, names and values go to the Rytm in SysEx, both ways.
- **Stepped knobs**: a P1..P16 knob (a sound word as before, p-lockable) whose 14-bit value picks one of `steps`
  entries (2..16256), one entry per Rytm detent: a list of 361 algorithms, a 19 x 19 pair of operator waves ("B3 /
  B12"). The Rytm needs the step count to move one entry per detent and keep the 14 bits; the host tells it.

Older peers ignore all of it: an older Rytm build drops command 52 (one SRC / SMPL page, plain 7-bit knobs); an older
Sidecar never reads the manifest's new block (no P17..P32: the plugin keeps its `voice_init` defaults) and keeps a kit
file with UMX lines as a file with lines it does not read (never written over).

## The plugin (sdk/rmp/rmp_api.h, rmp_api3.h)

```c
#define RMP_PARAMS_HI 16          /* P17..P32 (params_hi): SRC's second page P17..P24, SMPL's P25..P32 */
#define RMP_P_PARAMS_HI 5         /* rmp_plugin.reserved[5] (reserved[4] is API 3's RMP_P_V3) */
typedef void (*rmp_params_fn)(void *state, const float *p);
static inline rmp_params_fn rmp_params_hi_of(const rmp_plugin *p);   /* NULL from an API 1 table */
/* stepped knobs: v14 = value * 128 + fraction, 0..RMP_STEP_V14_MAX (16256 = 127.0) */
#define RMP_STEP_V14_MAX 16256u
#define RMP_STEPS_MAX 16256u
static inline uint32_t rmp_step_v14(uint32_t steps, uint32_t q);        /* entry q's 14-bit value */
static inline uint32_t rmp_step_entry(uint32_t steps, uint32_t v14);    /* the entry a 14-bit value means */
static inline uint32_t rmp_step_entry_of(uint32_t steps, float value);  /* ... a params() value p[k] */
RMP3_CAP_UM_P32 = 1u << 10       /* rmp3_host.caps: the host holds a machine's P17..P32 and calls params_hi */
```

- `params_hi(state, p)`: `p[0..15]` = P17..P32 as floats (0..127, no fraction), a P without a `knobs_hi` knob 0.
  Called on the control core between render calls like `params()`: once after `voice_init` / `voice_enter` and the
  first `params()`, **before the voice's first render**, then whenever one changes; every state of the track (its
  poly instances, a unison stack) gets the same values. NULL in the table: no P17..P32 (the feature off).
- **API 2** and **API 3** machines use the same function. API 3 (this engine): a machine's P1..P16 also arrive through
  `params()` (b->p is NULL on a machine: the host has no `RMP3_CAP_BLOCK_P`), so P17..P32 come the same way rather
  than through `b->p` or a new block pointer; `RMP3_CAP_UM_P32` in `rmp3_host.caps` (read in `init`) says the host
  calls it. An effect is never called (its P5..P16 are `RMP3_CAP_FX_P16`'s, in `b->p`).
- `RMP_API` stays 2: the slot was reserved and NULL in every table and an older host never reads it, so a plugin
  that fills it loads everywhere; start P17..P32 at the manifest's defaults in `voice_init` for an older host.
- The host checks the pointer is inside the plugin's code (else BAD "its params_hi is outside its code").
- A stepped knob: `uint32_t q = rmp_step_entry_of(steps, p[k]);` (the same integers as the host and the Rytm).
  `v14(q) = (q * 16256 + (steps - 1) / 2) / (steps - 1)`, `q(v14) = min((v14 * (steps - 1) + 8128) / 16256, steps - 1)`:
  entry 0 is 0.0, the last 127.0 (and 16383, the Rytm's 127 + 127/128), every entry round-trips, q is monotonic.

## The manifest (tools/rmp_pack.py, rmp_format.h RPG1)

Keys: `knobs_hi` (up to 16 knobs, `p` 17..32 each once, the fields of `knobs`, `role` free only, no text `preset`),
`page3_title` / `page4_title` (13 characters, "" the host's default), and per P1..P16 knob `steps` (2..16256), text
`pair` + `pair_names` (2..36 names of <= 7 characters, `steps` = n x n, entry q = `names[q / n] + " / " + names[q % n]`),
`default14` (0..16256). Packed as one block after the name table and **before** an RLB1 / RLB2 block (old readers look
at the end only and still find their labels): `nhi, nstep, nnames, 0, title3[13], title4[13], nhi x rmp_knob3 (26 B),
nstep x rmp_step (8 B: p, kind 0 number / 1 pair, u16 steps, u16 def14, name_first, name_count), nnames x 8 B names,
u16 length, "RPG1"`. The package format does not change (3, or 4 / 5 for their own reasons). Details and refusals:
docs/plugins.md section 3 "Pages 3 and 4, stepped knobs". The C reader: `rmp_manifest_pages` (rmp_manifest.h).

## The Sidecar (core/user/um.cpp, core/um_hi.h)

- **Scan**: each package's RPG1 block (`Pkg::pg`) and its name table (`pgnames`, on the Pi) are read with the manifest.
- **Values** (`core::umhi::track[vi]`: `v[16]`, `slot`, `tag` = the slot plugin's fingerprint, `n` = the highest
  P named - 16): a voice on a READY user slot whose values are another machine's (another slot, another fingerprint,
  or none after a kit load without UMX lines for it) takes this machine's manifest defaults (`hi_follow`, each idle
  step and each report). A voice on a built-in machine keeps what it held. Until the follow, params_hi gets the
  manifest's defaults, never another machine's values.
- **Kit file**: the kit's INSERT file (`/.rytm/ins/<project>/kNNN.txt`, core/fx/insert_card.cpp), four values a line:

  ```
  UMX <V1..V8> <first P 17|21|25|29> <slot 1..5> <tag 4 hex> <v> <v> <v> <v>
  UMX V1 17 1 2e10 10 33 44 1
  ```

  written for every voice that holds values (P17..P(16 + n)), read back on the kit's load (another kit's load first
  forgets every voice's: the defaults come back unless its file has UMX lines). A turn (the Rytm's, the console's)
  marks the file; a follow's defaults do not. A malformed UMX line (voice past V8, P outside 17..32, slot outside 1..5,
  a bad tag) is a line this build does not read: the file is kept, never written over. An older Sidecar treats every
  UMX line that way (it keeps the file, it does not save that kit's other settings either, as for XFX lines).
- **Console**: `umknob` (every voice's values: `V1 U1 P17..P32 10 17 ...`), `umknob=V:P:VAL` (voice 1..8, P 17..32
  that the voice's machine has, VAL 0..127: as a turn on the Rytm, the file marked).
- **Display and web**: `hooks.user_value_text(vi, k)` gives a stepped P1..P16 knob's text (a pair's "B3 / B12", a
  number's entry); the Pi's VOICE page shows it under the knob, the web's `voices` topic carries it as `vtext`
  (only when one of P1..P8 has one). The Rytm-screen pages the Sidecar draws (core/ui) have no user machine page.

## The protocol: 52 UM PAGES (sim/protocol.py, rmp_proto.h)

Every message is `F0 7D 52 54 52 <ver 1> <kind> ... F7`; a receiver drops another ver, kind or length. Text fields
are printable ASCII (else `_`), NUL-padded, not NUL-terminated when full. 14-bit numbers are two bytes, `v >> 7`
then `v & 7F`. `slot` is 1..5 (U1..U5), `voice` 0..7 (the physical voice, as in 1C), `gen` the slot's 1A generation.

| kind | dir | body after `<kind>` | bytes |
|---|---|---|---|
| 0 HEAD | host -> Rytm | `<slot> <gen> <nhi 0..16> <pages> <last p 0\|17..32> <nstep 0..16> <page3 title 13> <page4 title 13>` | 40 |
| 1 HI KNOBS | host -> Rytm | `<slot> <gen> <n 1..4>` then n x 1A's 24-byte knob record: `<p 17..32> <look> <kind> <role 0> <min> <max> <default> <enum count> <long 12> <short 4>` | 7+3+24n+1 |
| 2 STEPS | host -> Rytm | `<slot> <gen> <n 1..8>` then n x `<p 1..16> <kind 0 number\|1 pair> <steps hi> <steps lo> <def14 hi> <def14 lo> <pair names 0\|2..36>` | 7+3+7n+1 |
| 3 PAIR NAMES | host -> Rytm | `<slot> <gen> <p 1..16> <total 2..36> <first> <n 1..12>` then n x `<name 7>` | 7+6+7n+1 |
| 4 VALUES | both | `<voice 0..7> <slot 1..5> <first p 17..32> <n 1..16> <value 0..127> x n` (not past P32) | 7+4+n+1 |
| 5 ASK | Rytm -> host | `<slot 0 all \| 1..5>` | 9 |

`pages`: bit 0 = SRC page 2 exists (a knob in P17..P24), bit 1 = SMPL page 2 (a knob in P25..P32). `last p`: the
highest P named. A list knob of P17..P32 (look list, kind enum): its names are asked as P1..P16's are, `19 ASK what 1
(ENUM) arg P - 1` (16..31), and come back in 1B with `list` = that arg (an older host answers an empty list).

### When the host sends

- **HEAD, HI KNOBS (ceil(nhi / 4) messages, p order), STEPS (ceil(nstep / 8)), PAIR NAMES (per pair knob,
  ceil(total / 12))**: right after **every 1A part 0 + knob parts** the host sends for a slot (the answer to `19 ASK
  META` with another generation, i.e. whenever the slot's description is new to the Rytm), in that order, with that
  1A's `gen`; and for the slot (or all five) on **52 ASK**. A slot whose package has no block, an EMPTY or MISSING
  slot: a HEAD with nhi 0, pages 0, last 0, nstep 0 (and nothing more). A 1A answered "unchanged" (part 7F) is not
  followed by them: the Rytm keeps what it holds.
- **VALUES** (P17..P(last p), all of them, first 17): for each voice on a user slot whose machine has P17..P32,
  whenever they differ from what the Rytm was last told (the machine changed and took its defaults, a kit load, the
  console), every 2 s for every such voice, and after a **hello (35)** and a **52 ASK**. Only once the Rytm was heard:
  a 52 from it, a hello, or a `19 ASK META`.
- The host takes a **VALUES from the Rytm** (a turn) when its slot is the one the voice's values belong to (the
  voice's machine, READY, its fingerprint) and every P is one the machine has (P17..P(last p)); it hands the plugin
  the values at once, marks the kit's file and does **not echo** them. Otherwise it drops it (and the next refresh
  puts the Rytm right).

### What the Rytm does (sidecar-mods 0021 / 0029)

- **Keep per slot** (5) what HEAD .. PAIR NAMES said, with the gen; a 1A part 0 of another gen for the slot drops it
  until the HEAD that follows; a HEAD replaces all of it (its nhi / nstep say how many KNOBS / STEPS entries follow).
- **Pages**: a track whose sound plays a user slot with `pages` bit 0 gets **SRC page 2** (A..H = P17..P24), bit 1
  **SMPL page 2** (A..H = P25..P32), as 0029 gives a user effect's SEND FX pages 2 / 3 (issue #15): **SRC (or SMPL)
  held + DOWN** goes to page 2, **+ UP** back to page 1, no cycling; a SRC / SMPL tap with no arrow is given to stock
  (the page key's own cycle). The title is `page3_title` (SRC 2) / `page4_title` (SMPL 2), else the stock title with a
  `2/2` suffix. Each encoder shows its HI KNOBS record: short name as the label, long name in the upper left while it
  turns, look / kind / min / max as on page 1 (a list: its 1B names, zone `value * count >> 7`); a P without a record
  is drawn empty (id 0) and does nothing. A track on another machine, or a slot whose pages bits are 0: one page
  each, nothing held back.
- **Values**: per voice V1..V8, 16 bytes, from the host's VALUES (taken unless a turn of the same P is pending or was
  sent within the hold of 0029's crossing rule, as 22 ver 3). A turn sends VALUES for that voice with the voice's
  slot (at most every few ticks, the turned P or a run of them). These are **not sound words**: a trig held on page 2
  sets no lock, the kit's words and the pattern are never written, the values do not follow a kit copy on the Rytm
  (the host's kit file holds them).
- **Stepped knobs** (P1..P16 of a STEPS entry, on page 1 as before): the knob's sound word keeps its 14 bits (value
  byte + fraction, the fine flag set as for 0021's fine-flagged records), and **one encoder detent moves one entry**:
  from the word's v14, `q = min((v14 * (steps - 1) + 8128) / 16256, steps - 1)`, `q' = clamp(q +- detents, 0,
  steps - 1)`, write `v14' = (q' * 16256 + (steps - 1) / 2) / (steps - 1)` (exactly that entry's value: the plugin
  reads q' back); a held FUNC (fine) moves one entry too, never past the ends. Value text: kind number, the entry
  `q`; kind pair, `names[q / total] + " / " + names[q % total]` (up to 17 characters: cut to the field, keep the
  " / "). The default (`def14`) is what a knob reset writes. P-locks of a stepped knob store its 14 bits like any
  fine record (sim/protocol.py `step_detent` is the reference).
- **Ask** `52 ASK <slot>` (or 0) when it needs the pages again (a boot without a 1A exchange, a lost message), and
  ignore 52 of another version or a malformed one.

Reference values: `sim/protocol.py` (`pg_*`, `decode_pg`, `step_v14`, `step_entry`, `step_detent`, `pair_text`) is
byte-for-byte the C builders' (`sim/tests/test_protocol_um_pages.py` CrossCheck).

## Tests

- `core/tests/test_um_pages.cpp` (`make -C core test`; arm64 Mac): the test machine `plugins/api3_test/pagesprobe.c`
  (`api3_pages` dual API 3, `v2_pages` plain API 2: 32 knobs, P31 none, P4 a 361-entry pair, P5 a stepped number)
  - the 52s after a 1A (counts, titles, records, the pair names in two messages), none for a package without the
    block, P20's names on ASK ENUM 19;
  - defaults before the first render (P31 0), RMP3_CAP_UM_P32, a turn taken without an echo, five refused (another
    slot, past P32, voice 8, version 2, a byte over 127), the pair text (N10 / N10 .. N19 / N19 at 16383);
  - poly instances handed at their voice_init and on a turn; the kit file's UMX lines (a turn writes them, another
    kit's load gives the defaults, a malformed line locks the file, back gives the values); a machine change (API 2's
    render); a machine without P17..P32; `umknob`, a hello, the 2 s refresh.
- `core/tests/test_insert.cpp` "the UMX lines round trip"; `teensy/libraries/RytmPlugins/extras/proto_test.cpp` (the
  builders and parsers); `sim/tests/test_protocol_um_pages.py` (C cross-check, round trips, refusals, detents);
  `tools/tests/test_rmp_pack.py` Pages / PagesC and `extras/manifest_test.cpp` (the block); `tools/tests/test_rmp_api.c`;
  `pi/engine/tests/test_ws.cpp` (the `vtext` JSON).
- Rebuild the test packages after a change to pagesprobe.c: `make -C plugins/api3_test V=pages a64 && make -C
  plugins/api3_test V=pages2 a64` (an AArch64 gcc or the rytm-circle Docker image), copy `api3_pages.a64.rmp` and
  `v2_pages.a64.rmp` to `core/tests/api3_pkgs/`, and delete the built ones under plugins/ (test_api3_compat compares
  every package it finds there with the API 2 golden file).
