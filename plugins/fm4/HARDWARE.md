# FM4 initial Pi observation — 2026-11-10

**Result: initial hardware playback observation passed for this session.** The user reported stable sound with no unintended clicks, dropouts, stuck notes or sudden level jumps. This is user listening evidence; no audio was recorded by the agent.

This supplements the earlier native-only [validation report](VALIDATION.md). It is not a worst-case DSP benchmark, full algorithm/parameter qualification, polyphony approval, long-duration soak, or release approval.

## Exact identity and scope

- Pi Sidecar 0.4.3, clean commit `1debc00af3d136637d82101b7f0d248cd55dd9a7`; reported build Nov 8 2026 20:18:15.
- FM4 0.1.0 / `fm4.pm4`, API 2, loaded in U5, state 1200 bytes, provisional cpu_permille 6322, mono manifest.
- Installed package read back and byte-compared with the local artifact: exact match, SHA-256 `505324db00f6f652f419ba6ad6e547b83a132570a1bc5c159ff4cb354f66f28f`.
- 120.002 seconds, 121 status/plugin snapshots and 120 engine timing log samples, starting 2026-11-09 19:53:54 UTC / November 10 03:53:54 Taipei.
- User confirmed the proposed single-FM4-track run and operated the instrument. Other existing engine machines remained present. Requests were GET-only after session pairing; the agent sent no notes, changed no audio settings, and performed no deployment or device recording.
- User was asked to cycle all algorithms/macros/retriggers. No parameter/event trace was captured, so exact coverage and active-note duty cycle are unverified.
- Checked timing implementation in the installed commit: no differences from the inspected checkout in core/user/um.cpp, pi/engine/cores.cpp, pi/engine/guard.h or pi/engine/platform_pi.cpp.

## Observed results

| Measurement | Result |
|---|---:|
| New steady-state underruns | 0 |
| New restart underruns / stream resets | 0 / 0 |
| Muted voices / fault warnings observed | 0 / 0 |
| Transport gap/lost/error/reset counters observed | All zero |
| Actual clock reported by firmware measurements | 1500 MHz throughout snapshots |
| Temperature | 56–58 C |
| Current undervoltage / throttling / clock-cap warnings | None |
| FM4 render calls added | 479,990 |
| Render-core 1 busy share | 6.5–7.6% |
| Render-core 2 busy share | 0.7% |
| Control-core busy share | 12.5–14.0% |
| Largest logged engine render batch | 27 us |
| Longest logged control pass | 118 us |
| Minimum logged audio FIFO | 71 frames, target 96 |
| HTTP errors / log cursor gaps | 0 / 0 |
| Slot state | READY throughout |

FM4's lifetime-average cost changed from 4592 ns across 5,148,732 calls to 4907 ns across 5,628,722 calls. Differencing average×count estimates this interval's mean at **about 8.3 us per 12-frame call**, or **about 3.3% of one core** at 48 kHz. This is approximate: the lifetime averages are rounded/truncated by the host, and the call population can include silent blocks. It is not a directly exposed interval timer or an active-only mean.

The **27 us engine batch maximum is not an FM4 call maximum**. It includes waiting/scheduling and both render workers. Neither that number nor the absence of glitches establishes the <25 us worst-case FM4 target. No CPU declaration or admission limit was changed based on this observation.

## Remaining work

- [x] Initial exact-package Pi playback observation with user listening confirmation.
- [ ] Controlled per-algorithm held-note and extreme-setting trials with known parameter vectors.
- [ ] Per-plugin maximum/timing-distribution evidence; current HTTP telemetry does not expose it.
- [ ] Ten-minute realistic-load soak.
- [ ] Separately prepared/admitted two- and four-instance polyphony candidate and whole-engine load tests.
- [ ] Measurement-derived CPU declaration, final compatibility/release recommendation.

Evidence: [summary](build/pi/20261109T195354Z/summary.json), [timestamped observations](build/pi/20261109T195354Z/observations.jsonl), [engine log](build/pi/20261109T195354Z/engine.log), [package identity](build/pi/20261109T195354Z/identity.json). The summary includes hashes of the raw evidence. Build-directory evidence is local and git-ignored.
