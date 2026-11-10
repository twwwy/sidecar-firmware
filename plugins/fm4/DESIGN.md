# FM4 mapping contract — compatible 0.1.0 / 0.1.1

This implements the approved Phase 1 design. Identity `fm4.pm4` is stable. All sound mappings below are frozen for compatible releases. Raw defaults are `[0,72,64,52,24,96,64,0,0,64,72,8,80,64,0,100]`.

## API binding

Authority is the checked-out `sdk/rmp/rmp_api.h`, `sdk/rmp/plugin.mk`, `tools/rmp_pack.py`, `core/user/um.cpp`, `core/inst_alloc.{h,cpp}`, and `pi/engine/{cores.cpp,guard.h,kernel.cpp}` at `c442c6db407bf01a51530e82deac23308eeb5a67`.

API 2: init(host), voice_init(state), params(state,p[16]), note_on(state,note,velocity), note_off(state), render(state,out,n), optional voice_enter/voice_leave. No load_entry or parser. The host zeroes and 8-aligns state. Its current Pi configuration is 48000 Hz/max_block 12; DSP uses supplied rate and n. Initialization accepts finite 8000…768000 Hz (the wider range supports offline references), positive max_block, API exactly 2. Render overwrites mono floats. No audio input, stereo, tempo, transport or custom display API is assumed.

On first use: zero → voice_init → voice_enter → params → note_on; params may occur without a note. Note delivery can be deferred by the host. Leave clears activity/history. The host controls state between render blocks; separate instances may render concurrently. Global tables are const; sample rate is frozen after image init. No per-instance state is shared.

Manifest: 16 unique P1…P16 controls; page2=params, title FM4 SHAPING; names <=12 characters, short labels <=4; ALGO and GATE use list zones. No additional manifest keys. This produces RMPA format 3, API 2, 128-byte AArch64 entry. Pi state maximum is 65536 bytes; FM4's own static budget is <4096.

## Operator graph

A/B/C/D are indices 0/1/2/3. Each operator is `gain × sin(phase + incoming radians)`. Evaluation order D,C,B,A is acyclic for every current-sample graph. Copy all old outputs before any evaluation; delayed sources always read the copied vector. Store all new outputs only after the graph completes. Sine table: 4096 intervals, endpoint included, linear interpolation.

| Algorithm | Current edges | Delayed edges | Carriers | Anchor |
|---|---|---|---|---|
| STACK | D→C, C→B, B→A | D→D | A | A |
| DUAL | D→C, B→A | D→D, B→B | A,C | A |
| FORK | C→B, B→A | C→C | A,D | D |
| FAN | D→A, C→A, B→A | D→D, C→C, B→B | A | A |
| SPLIT | D→C, D→B | D→D | A,B,C | A |
| CROSS | D→C, B→A | A→D, C→B | A,C | A |
| RING | D→C, C→B, B→A | A→D | A | A |
| PARA | none | A→A, B→B, C→C, D→D | A,B,C,D | A |

RING names a PM loop, not amplitude ring modulation. There is no additional dry oscillator.

## Units and curves

Continuous raw p saturates at 0…127, x=p/127. Bipolar B(p)=(p−64)/64 below 64 and (p−64)/63 otherwise, exact neutral at 64. Invalid nonfinite parameters retain their last valid targets. Lists use integer-zone decoding `(int(p)*N)>>7`; saturated input prevents unsafe indices. Note clamp 0…127; velocity clamp 0…1; invalid notes/velocities are ignored.

| Parameter | Internal mapping | Smoothing time constant |
|---|---|---|
| ALGO | Eight 16-value zones | 5 ms finite crossfade |
| RAT | Ratio table below; smooth log2 ratios | 10 ms |
| COL | c=B(p), −1…1 | 5 ms |
| FM | I=8(2^(3x)−1)/7 rad | 3 ms |
| FDBK | F=2.5x² rad | 5 ms |
| SWP | s=B(p), −1…1 | 5 ms |
| DEC | TD=.020×200^x s (20 ms…4 s) | 10 ms time smoothing |
| MRPH | m=x | 10 ms |
| ATK | TA=.001×2000^x s (1 ms…2 s) | 10 ms time smoothing |
| REL | TR=.010×800^x s (10 ms…8 s) | 10 ms time smoothing |
| BODY | b=x | 5 ms |
| DRFT | 30x² cents | 50 ms |
| VEL | sensitivity x, note-latched | Next note |
| TUNE | 100B(p) cents | 5 ms |
| GATE | 0…63 ONE; 64…127 GATED | Next note |
| LEV | .8x² linear amplitude | 5 ms |

One-pole smoothing is `v += (target−v) × (1−exp(−1/(tau×Fs)))`. At a fresh note, set smoothers to targets. An audible retrigger first fades to zero, then applies targets. Live knobs, locks and host modulation are indistinguishable in params(), so use the same live policy. Time edits change the current stage's rate without restarting it. Derived frequency/edge/carrier coefficients update on a sample-counter-based 16-sample cadence and interpolate over 16 samples. Envelope coefficients update on that cadence. This remains independent of block partitioning.

## RATIO and MORPH

| Raw landing | A | B | C | D |
|---:|---:|---:|---:|---:|
| 0 | 1 | 1 | 1 | 1 |
| 18 | 1 | 2 | 3 | 4 |
| 36 | 1 | 3 | 5 | 7 |
| 54 | 1 | .5 | 1.5 | 2 |
| 72 | 1 | 1.414214 | 2.414214 | 3.141593 |
| 90 | 1 | 1.003 | .997 | 2.007 |
| 108 | 1 | 1.618034 | 2.718282 | 4.236068 |
| 127 | 1 | .25 | 1.414214 | 5.196152 |

First/last 10% of each interval are plateaus. Through the middle 80%, interpolate log2 ratios with t²(3−2t). A stays at ratio 1. MORPH adds m×[0,17,−23,31] cents, multiplies source-edge strengths by 1+m×[0,−.20,.25,−.15], feedback by 1−.20m, and carrier weights by 1+.20m×z[i]. It never changes ALGO, reseeds anything or oscillates by itself.

Frequency = 440×2^((note−69)/12 + log2(ratio) + (TUNE+morph_cents+drift_cents)/1200), capped individually at .20×Fs. This intentionally distorts relationships at very high notes. It is not a sideband anti-aliasing guarantee.

## COLOR, SWEEP, FM, BODY

z[A,B,C,D]=[−1,−1/3,+1/3,+1]. COLOR multiplies each current edge from j by 2^(c×z[j]). Divide by the destination's current-edge count (FAN has three). The same tilt affects carrier weights. PARA also multiplies each self-feedback coefficient by 2^(.5c×z[i]). Thus COLOR changes spectral construction rather than being an output gain.

Modulation contour e starts at 1 and multiplies by exp(−ln(1000)/(TD×Fs)), reaching .001 in DEC seconds; continue to 1e−7 and snap to zero. Sweep Q=2^[2s(2e−1)]. Positive starts bright then closes; negative starts at reduced modulation and opens. At s=−1 Q goes .25→4.

Activity u=1−abs(s)+abs(s)×(e for s>=0, else 1−e). Carriers have gain 1; noncarriers gain .35+.65u. No separate operator editors/envelopes.

Velocity timbre multiplier = 2^[2×VEL×(velocity−1)], latched on note-on. Effective current-edge index = min(16,I×Q×velocity_timbre). FM=0 removes all current-edge modulation; feedback remains. A clean pitch fixture also sets FDBK=0.

BODY: anchor carrier base weight 1+3b, others 1; apply COLOR/MORPH weighting, then divide every weight by their sum. All PM edges into the anchor (including feedback) multiply by (1−b)². In STACK/FAN/RING this cleans A; it does not add a dry path. DUAL/CROSS clean and emphasize A; FORK emphasizes its independent D; SPLIT emphasizes its independent A; PARA emphasizes A and removes A's feedback at maximum BODY.

Feedback uses the explicitly delayed edges and F, with MORPH/BODY multipliers. PARA's COLOR factor permits at most 2.5√2 rad; other algorithms at most 2.5 rad. Bounded oscillator output does not imply bounded aliasing or consistent perceived loudness.

## Amplitude, retrigger and transitions

Envelope is normalized internally; multiply by velocity exactly once. VEL never changes amplitude velocity behavior. Zero velocity is silent in the core (the normal host trigger path clamps to at least 1/127).

GATED: linear 0→1 attack in TA; sustain 1 while held; on note-off multiply the current amplitude by exp(−ln(100000)/(TR×Fs)). Accumulate release progress as 1/(TR×Fs); at progress 1, clear amplitude/history/DC to exact silence. This retains the relative −100 dB release endpoint even when note-off occurs during attack. Live REL changes change the remaining rate.

ONE: attack; exponential body to .25 in TD using exp(−ln(4)/(TD×Fs)); then the same relative −100 dB release in TR. Nominal duration TA+TD+TR (sample quantization/coefficient rounding apply). Note-off does not shorten it. GATE changes latch on next note.

Audible retrigger fades output to zero over ceil(.001×Fs) samples. Latest note replaces the pending note; the fade is not restarted by repeated notes. At zero reset phases/history/envelopes, set targets, and attack. A note-off during the wait marks the pending gated note released. Silent notes start immediately. Drift state continues across retriggers; all four oscillator phases reset to zero.

ALGO: two banks maximum. Copy phases/old outputs, run the old/new routes concurrently, linearly blend for ceil(.005×Fs) samples. A second change only replaces the latest requested algorithm; finish the current transition, then transition to that request. No queue and no third bank. A retrigger supersedes the algorithm transition at the zero crossing of its fade. Temporary DSP cost approaches twice the graph cost.

## Drift and output

Four local xorshift32 streams: seed XOR (0x9e3779b9×(i+1)), nonzero fallback. Fixed adapter seed 0x464d3401. Periods [.73,1.07,1.61,2.29] s; smoothstep interpolation between uniform targets in [−1,1]. Depth multipliers [.2,1,.7,1] times DRFT cents. Runs while active; no tempo or host random service is required. Identical initialized voices/events can have identical drift, but no state is shared. Native seed is configurable.

Normalized carrier mix → algorithm blend → envelope×velocity → 10 Hz DC blocker → LEV → retrigger fade → ±.95 safety clamp. Applying the retrigger fade after the DC blocker ensures the whole audible tail reaches zero before resetting it. The blocker is y=x−old_x+R×old_y, R=exp(−2π10/Fs). Count nonfinite recovery and clamp samples; record pre/post-clamp peaks. Nonfinite output clears the instance; tiny envelopes/smoothers/filter state snap to zero. No native diagnostic counter is image-global.

The only implementation refinements to the approved specification are precise sample rounding, relative release progress tracking, and placing retrigger attenuation after DC management so reset cannot expose the filter tail. They introduce no extra parameter or preset mapping change.

## Offline reference limits

Reference builds use sinf, 8×/16× sample rate, the same .20×original-Fs cap, the same control cadence in real time, and a feedback delay of 8/16 internal samples (one original sample). A separate 8× one-internal-sample loop demonstrates delay sensitivity. Downsample with a unity-DC Blackman-windowed sinc, cutoff .45×host Fs, support ±32 host frames. Compare steady sections and normalized spectral energy, not chaotic waveform identity. Remaining 8×/16× divergence means the extreme-feedback reference is not a converged numerical alias oracle.

0.1.1 implementation note: fixed-graph specialization, moving-smoother masks and envelope coefficient caching preserve this mapping. See POLYPHONY.md for version-specific state size, capability versus admitted voices, and validation.
