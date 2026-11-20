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
  (only when one of P1..P8 has one). The Rytm-screen pages the Sidecar draws (core/ui) have no user machine page of
  their own; a machine's plugin may draw its track's SRC / SMPL pages itself (below, "Plugin-drawn screens").

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

## Plugin-drawn screens

A user machine may draw the Rytm's screen itself while one of its track's pages shows: the stock SRC page (the SRC view,
rytm-mods 0026 rui1027: today drawn by the host only for its built-in machines), the stock SMPL page, and SRC / SMPL page
2 (P17..P32, above). Every knob and key stays the Rytm's (and 0029's on page 2); only the picture is the plugin's. A
plugin without `draw`, or one that declines a page, keeps what the page showed before.

### The plugin (sdk/rmp/rmp_api.h, rmp_api3.h)

```c
/* rmp_plugin.reserved[RMP_P_DRAW]: the machine draws its own Rytm screen (NULL: the host's default) */
#define RMP_P_DRAW 6
#define RMP_DRAW_W 128
#define RMP_DRAW_H 64
#define RMP_DRAW_BYTES 1024              /* 1 bit a pixel, row-major, 16 bytes a row, MSB = the leftmost pixel, 1 = lit */
enum { RMP_PAGE_SRC = 1, RMP_PAGE_SMPL = 2, RMP_PAGE_SRC2 = 3, RMP_PAGE_SMPL2 = 4 };
enum { RMP_DRAW_LOCKED = 1u << 0 };      /* flags: P1..P16 include a held step's locks */
typedef int32_t (*rmp_draw_fn)(void *state, int32_t page, uint32_t flags, int32_t track, const float *p, uint8_t *fb);
static inline rmp_draw_fn rmp_draw_of(const rmp_plugin *p);   /* NULL for api < 2 or a NULL slot */
RMP3_CAP_DRAW = 1u << 11         /* rmp3_host.caps: the host calls draw */
```

- `draw(state, page, flags, track, p, fb)`: `page` an `RMP_PAGE_*`; `p[0..31]` = P1..P32 as the page shows them (value +
  fraction / 128: P1..P16 the Rytm's, a held step's locks with `RMP_DRAW_LOCKED`; P17..P32 the host's, as `params_hi`
  gets them, the manifest's defaults until the track's values are the machine's; a P with no knob 0); every page gets
  all 32. `track` 0..11 (BD .. CB). `fb`: `RMP_DRAW_BYTES`, cleared by the host; pixel (x, y) from the top-left is lit
  when `fb[y * 16 + x / 8] & (0x80 >> (x % 8))`. Return 1: drawn (the frame replaces the whole screen, title bar
  included: the host adds nothing); 0: not handled (the host's default for that page).
- Called on the control core between render calls, **guarded** (Exec::guard, as API 3's control calls, for an API 2
  plugin too): a fault mutes the slot (and the slots sharing its image) until `um=reload`, the page goes back to stock.
  Keep it **under 1 ms**: three calls over 1 ms in a row (or one over the FIFO's stall limit) mute an API 3 plugin (an
  older one is only counted, as for its other calls).
- `state` is the voice state of the track's voice (`kTrackVoice`: BD 0, SD 2, RS / CP 7, BT 1, LT 3, MT / HT 5, CH / OH
  4, CY / CB 6) when that voice plays the machine now, else **NULL** (no trig yet, another machine on a shared voice): a
  draw must work from `p` alone. It may read the state (between render calls), never write what the sound needs.
- The pointer must be inside the plugin's code (else BAD "its draw is outside its code"). RMP_API stays 2 (an older
  host never reads reserved[6]: the plugin loads and plays, its pages stay stock there); API 3 machines see
  `RMP3_CAP_DRAW`.

### The host (core/ui/remote.cpp user_frame, core/user/um.cpp on_draw / on_draw_key)

- A view (below) of track t is drawn by the plugin when t plays a user slot (the 33's voice byte 0x50..0x70, else the
  voice's reports) that is READY, not muted, and whose plugin has `draw`. `hooks.user_draw_key(voice, slot)` says so (0:
  no) and hashes the inputs besides P1..P16 (the plugin, the voice's P17..P32, whether the voice plays it).
- **When**: once after each view message `on`, then whenever an input changes - P1..P16 (2A, the voice's own values
  until the 2A came), the page, `RMP_DRAW_LOCKED`, the track, P17..P32 (a 52 VALUES turn, a kit load, `umknob`), the
  plugin (a reload) - checked at the view's draw rate (25 ms, at once after a 2A). Never twice for the same inputs.
- **The frame**: `rows_to_fb` (core/ui/ui.h) turns the row-major bitmap into the canvas layout, the view's tile diff
  sends the changed tiles (`38` runs, `39`; a whole frame with `3E` after a view's `on`), no text items (`3D` empties
  the slots the frame before had). Commit pacing as the SRC view's (0.4.3 / 0.4.4): while the Rytm plays, at most every
  200 ms, a knob's own value (a 2A of one value, a held step's, a P17..P32 turn on page 3 / 4) every 100 ms; stopped:
  at once. `3C` keepalives while the view lasts.
- **Fallback**: `draw` returned 0, faulted, the slot muted / not ready, or no draw: page 1 shows what it showed before
  (a built-in machine's SRC view page, else stock), pages 2..4 stock. When the host had drawn the view, `3A 0 0` gives
  the Rytm its stock page back until the next commit. A declined page is asked again when an input changes.
- The TRK + YES page comes first (no draw while it is open); the SMPL / page-2 views do not take the SRC view's pushes
  (31 0x70..0x77 act on page 1 only). `rui=srclog=1` logs each draw ("track t page p drawn by Un: yes / declined").

### The protocol: the view message with a page (sim/protocol.py `rui_src_view`, `decode_rui`)

The SRC view's message carries the page in its track byte. Same command, version, length; host side only (the host
never sends 29).

| bytes | dir | meaning |
|---|---|---|
| `F0 7D 52 54 29 01 <t> <on> F7` | Rytm -> host | page 1, the stock SRC page of track t (0..11): unchanged since rui1027 |
| `F0 7D 52 54 29 01 <t \| 0x10> <on> F7` | Rytm -> host | page 2, the stock SMPL page of track t |
| `F0 7D 52 54 29 01 <t \| 0x20> <on> F7` | Rytm -> host | page 3, SRC page 2 (0029: P17..P24) |
| `F0 7D 52 54 29 01 <t \| 0x30> <on> F7` | Rytm -> host | page 4, SMPL page 2 (0029: P25..P32) |
| `F0 7D 52 54 2A 01 <t \| 0x40 held> <first> <n> {<int> <frac>} x n F7` | Rytm -> host | P1..P16 of the view's track: unchanged |

`<on>` 1 shown, 0 left. The track byte is `t | (page - 1) << 4`: bits 0..3 the track (0..11), bits 4..5 page - 1, bit 6
always 0 (reserved: the host drops the message). A receiver drops a track over 11, an `on` over 1, another length or
version. Examples: SD's SMPL page shown `F0 7D 52 54 29 01 11 01 F7`; CB's SRC page 2 left `F0 7D 52 54 29 01 2B 00 F7`;
BD's SMPL page 2 shown `F0 7D 52 54 29 01 30 01 F7`.

**Why this message**: the host's answer to a view is its frame (0026's `38` / `39` / `3C` / `3A`), and 0026 already
tells the SRC page with 29 and its values with 2A; the SMPL page and the second pages are the same kind of screen with
the same values (2A carries P1..P16 for all of them, P17..P32 are the host's own). One view at a time, one state machine
on both sides; 52 (0021 / 0029's slot protocol, routed to the user machines) stays about the pages' contents.

**Older peers**: an older host drops a 29 whose track byte is over 11 (counted as malformed, nothing sent): the Rytm
keeps its stock page (0029's own page 2 included), so the Rytm may send the page form unconditionally. An older Rytm
never sends it: the plugin draws page 1 only (the plain 29, which every 0026 since rui1027 sends).

### What the Rytm does (sidecar-mods 0026 / 0029)

- **0026, the SMPL page**: as 29 for the SRC page - `29 <t | 0x10> 1` when the stock SMPL page of track t shows (also
  on a machine change there and after each hello), `29 <t | 0x10> 0` when it leaves; for the tracks it sends 29 t 1 for
  today (a Sidecar machine; the host ignores the others). Then 2A exactly as for the SRC view: all 16 after the `on`,
  the changed span each tick, a held step's with 0x40.
- **0029, page 2**: while SRC page 2 of track t shows (SRC held + DOWN), `29 <t | 0x20> 1`; SMPL page 2 `29 <t | 0x30>
  1`; back to page 1 (+ UP) the page-1 view's `on` again (`29 <t> 1` / `29 <t | 0x10> 1`). 2A keeps coming for t as on
  page 1 (the plugin draws P1..P16 there too). 0029's own page-2 drawing stays what shows until the host's frame comes,
  and comes back when the host's frames stop.
- **One view at a time**: an `on` replaces the view before (a new page, a new track); switching pages may send the old
  view's `off` first or not - the host takes either. An `off` names the view it ends (the host ignores an `off` of
  another track or page). After a hello: the view's `on` again, as 29 t 1 today.
- **Showing the frame**: as the SRC view: the host's frame from the first `39` after the view's `on`, while `3C` /
  frames keep coming (without them, the SRC view's timeout: stock again); `3A x 0` with the remote page closed: stock again; nothing from the
  host: stock (0029's page 2 on pages 3 / 4). Every key and knob stays the Rytm's: the host sends no `37`, `3F` or
  `34` for a view; on pages 3 / 4 0029's encoders keep sending 52 VALUES (the host redraws from them).
- The SRC view's pushes (31 0x70..0x77: the source list, SAMPLE EDIT, CHOP's editor) are page 1's only; 0026 need not
  send them on the other pages (the host ignores them there).

### Tests

- `core/tests/test_um_draw.cpp` (arm64 Mac, `make -C core test`): `rows_to_fb` (a pattern, the corners); the probe's
  draw (`plugins/api3_test/pagesprobe.c`: page, flags, state, track, P1..P32 as bars, a fixed pattern; P2 1 declines,
  P2 2 faults) on the SRC view (page 1, the fraction, P17..P32, the state), redraws on a 2A / a held step's / a P17
  change and none on the same values, pages 2..4 by the track byte, an `off` of another page, malformed bytes, a
  built-in machine's SMPL view stock, the API 2 machine with no voice (state NULL), a plugin without draw (stock), a
  declined draw (`3A 0 0`, back), a fault (the slot muted, stock, no more calls), `RMP3_CAP_DRAW` as the API 3 probe sees it.
- `sim/tests/test_protocol_rui.py` `test_view_pages` (the bytes, every track and page both ways, refusals);
  `tools/tests/test_rmp_api.c` (`RMP_P_DRAW`, the constants, `rmp_draw_of`).

## An effect's pages (sendfx-draw)

A user effect (`RMP3_KIND_EFFECT`, docs/plugins.md 12b) may draw the Rytm's SEND FX pages of the send slot that plays it,
as a machine draws its track's pages (above). Every knob stays the Rytm's; only the picture is the plugin's, and a plugin
frame replaces the whole screen, so it draws the slot's own controls too. The pages (rytm-mods 0029):

| page | A | B | C | D | E | F | G | H |
|---|---|---|---|---|---|---|---|---|
| 1 `RMP_PAGE_FX1` | TYPE | ROUTE | BASE | WIDTH | P1 | P2 | P3 | P4 |
| 2 `RMP_PAGE_FX2` | P5 | P6 | P7 | P8 | P9 | P10 | P11 | P12 |
| 3 `RMP_PAGE_FX3` | P13 | P14 | P15 | P16 | | | | |

(TYPE, ROUTE, BASE, WIDTH are the Sidecar's; P1..P4 are the effect's knobs in the Rytm's kit words 38 / 39, p-lockable;
P5..P16 the Sidecar's, `RMP3_CAP_FX_P16`.) An effect without a draw keeps the Rytm's own pages, with its labels, as
before: no package changes or rebuilds for any of this.

### The plugin (sdk/rmp/rmp_api.h, rmp_api3.h)

The same `draw` as a machine's (`rmp_plugin.reserved[RMP_P_DRAW]`), called when the host has `RMP3_CAP_FX_DRAW`
(`1u << 12`):

- `page` `RMP_PAGE_FX1` / `FX2` / `FX3` (5, 6, 7); `track` the send slot (0 SEND FX 1, 1 SEND FX 2); `flags` 0;
- `p[0..15]` = P1..P16 as the effect's render gets them (P5..P16 its manifest's defaults until the slot's values are
  this effect's); `p[16..19]` (`RMP_DRAW_FX_TYPE`, `_ROUTE`, `_BASE`, `_WIDTH`) = the slot's TYPE (the send FX id),
  ROUTE (0..63 PRE, 64..127 POST), BASE and WIDTH (0..127, `core/fx/insert.h` sfx-basewidth); the rest 0;
- `state` **always NULL**: an effect renders on core 2 while the draw runs on the control core, so a draw works from `p`
  alone;
- guarded as a machine's (a fault mutes the effect, as a fault in its render does), under 1 ms.


### The host (core/ui/remote.cpp fx_frame / fx_tick, core/user/um.cpp on_fx_draw*, core/host_link.cpp)

- The SEND FX view is its own state (send slot, page), never a track view: one view at a time, a track's `on` ends it
  and its `on` ends a track's.
- **Fast**: the view's `on` draws at once (no wait for the 25 ms draw tick) and sends the whole frame (commit pacing
  does not hold a view's first frame). After that the inputs (P1..P20, the page, the plugin) are checked at the draw
  rate and the plugin is called on a change only; a knob's change is the user's own (`kSrcUserMs` pacing while the
  Rytm plays). Each slot's pages keep their last frame by its inputs: a page flipped back to with nothing changed is
  sent from there, with no plugin call (`remote_stats().fx_cached`). The tile diff sends only what changed.
- **No flash**: 22 ver 4 SENDFX DRAWS tells the Rytm which slots draw (below), so on a change to such a page it keeps
  what it shows until the host's first frame instead of showing its own page first. When the new view (a track's or a
  SEND FX page) is not drawn by a plugin while the view before was, the host sends `3A 0 0` at once (for machine views
  too): the Rytm's own page comes back without waiting for its hold to time out.
- Fallback as a machine's: the draw declining (0) or faulting, the effect muted or unloaded, another effect without a
  draw in the slot: the Rytm's own page (`3A 0 0` after ours).

### The protocol (sim/protocol.py `rui_sendfx_view`, `sendfx_draws`)

| bytes | dir | meaning |
|---|---|---|
| `F0 7D 52 54 29 01 <12 \| 13 + (page - 1) << 4> <on> F7` | Rytm -> host | SEND FX 1 / 2's page 1..3 shown (1) / left (0) |
| `F0 7D 52 54 22 04 <fx> <on> F7` | host -> Rytm | 22 ver 4 SENDFX DRAWS: FX fx's effect draws its pages (1) or not (0) |

The view is 29's track byte with tracks 12 / 13 (an older host drops a track over 11 as malformed: the Rytm keeps its
own page); page 4 and bit 6 stay malformed. The Rytm starts from 0 for both slots; the host sends a 1 (again with
BASE / WIDTH's 2 s refresh, on ASK and hello) and a 0 only after a 1 (a slot without such an effect costs nothing on the
wire). An older 0029 drops ver 4 (its ver check).

### What the Rytm does (sidecar-mods 0026 / 0029)

- **0029**: SEND FX page 1's encoders in the order above (BASE / WIDTH on C / D, the effect's P1..P4 on E..H: the words
  and their locks unchanged, only which encoder turns which); `29 <12 | 13 + (page - 1) << 4> 1` when a SEND FX page shows
  (page changes, a slot change, after a hello), `... 0` when it leaves; 22 ver 4 kept per slot.
- **0026**: the host's frame on a SEND FX page as on the SRC view (from the first `39` after the view's `on`, while
  `3C` / frames come). With DRAWS 1 for the slot, a change to its page keeps what the screen shows (whatever page was
  up) until that first commit (the hold of 0026's umhold, 300 ms at most); with 0 it shows its own page at once.

### Tests

- `core/tests/test_fx_draw.cpp` (arm64 Mac): the probe `plugins/api3_test` `V=fxdraw` (`api3_fxdraw.a64.rmp`): page 1's
  values (page, slot, flags 0, state NULL, the caps, P1..P16, TYPE ROUTE BASE WIDTH), pages 2 / 3, the page cache (back
  to a page: no draw), redraws on a knob and BASE / WIDTH and none on the same values, an effect without draw (PHASER:
  the Rytm's page, `3A 0 0`), PHASER / BBD / NIMBUS loading unchanged, one view at a time, malformed bytes, decline,
  fault (muted, no more calls), 22 ver 4 on ASK and after the fault.
- `sim/tests/test_protocol_rui.py` `test_sendfx_view` (the bytes, round trips, refusals).

## Tests (P17..P32, stepped knobs)

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
- Rebuild the test packages after a change to pagesprobe.c (test_um_draw.cpp reads them too): `make -C plugins/api3_test V=pages a64 && make -C
  plugins/api3_test V=pages2 a64` (an AArch64 gcc or the rytm-circle Docker image), copy `api3_pages.a64.rmp` and
  `v2_pages.a64.rmp` to `core/tests/api3_pkgs/`, and delete the built ones under plugins/ (test_api3_compat compares
  every package it finds there with the API 2 golden file).
