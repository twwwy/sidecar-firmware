# FM4 local validation report

This is the preserved pre-hardware native report. Subsequent initial Pi evidence is recorded separately in [HARDWARE.md](HARDWARE.md).

Candidate only. Implemented, tested natively and built for Pi; **not tested on Pi**. Subjective listening is pending. No deployment or publication occurred.

Base revision: `c442c6db407bf01a51530e82deac23308eeb5a67`. The implementation is an uncommitted addition; the base revision alone does not identify it.
Actual implementation source-set SHA-256: `f229dcd7c60971436e780d17410da15ee2f1728d9ae286775ff461a8f3db58ea`. Individual source hashes and package/toolchain custody are in `build/identity.json`.
Package SHA-256: `505324db00f6f652f419ba6ad6e547b83a132570a1bc5c159ff4cb354f66f28f` (33532 bytes).

## Scope and evidence

- `make check`: explicit routing references for all eight algorithms; API lifecycle; 44.1/48/96 kHz; envelope timing; velocity/endpoints; repeated notes; rapid parameter changes; 20,000 randomized blocks; latest-target algorithm switching; exact retrigger fade shape; deterministic block-partition invariance; independent interleaved states; finite/bounded output; level-zero and post-release silence.
- `make sanitize`: same suite under AddressSanitizer and UndefinedBehaviorSanitizer, passed.
- Clean A4 pitch error approximately −0.00070 cents in the native test (tolerance 1 cent). Maximum absolute sustained-test DC approximately 0.00022693 (threshold 0.001). Random-stress clamp count zero.
- `make package-check`: repository loader validates target, CRC and manifest, relocates the exact RMPA image and executes all eight algorithms with four independent states on this Apple-silicon Mac. Also rejects wrong-target and corrupt-CRC packages. Not a host admission test or Pi test.
- Nine native fixtures generated at 48 kHz, 12-frame blocks, seed 0x464d3401, velocity 0.85; parameters, note/off timeline, source hashes and WAV hash accompany each.
- Eight extreme high-note algorithm comparisons against offline 8× and 16× references, with preserved host-time feedback delay; separate short-delay variants measure delay sensitivity.

## Memory and package

Production instance state: **1,200 bytes**, including both transition banks and diagnostics. Below the 4 KiB FM4 assertion and 65,536-byte Pi limit. No render-time allocation.
Package image: 32768 bytes; BSS: 8 bytes; relocations: 11. Immutable math tables hold 4097 sine floats and 1025 fractional-exp2 floats (20,488 bytes). Shared writable BSS is only initialized host configuration; all voice state is separate.

## Native timing — regression evidence only

Host: Apple M1 Max, macOS-15.7.4-arm64-arm-64bit-Mach-O; Apple clang version 17.0.0 (clang-1700.6.4.2), `-O2`. The Mac CPU clock was not fixed or measured. Timings do not prove Pi headroom.
Each row averages 10,000 timed 12-frame groups after 1,000 warm-up groups. Covers all algorithms, defaults, high-note maximum FM/feedback/drift, and frequent parameter/algorithm changes. Groups render states serially; this is not the host mixer or whole-engine load. Individual observed maxima include OS scheduling and instrumentation disturbances.

| Independent states per group | Minimum scenario mean (us) | Maximum scenario mean (us) | Largest observed group (us) |
|---:|---:|---:|---:|
| 1 | 1.350 | 2.880 | 66.875 |
| 2 | 2.688 | 5.680 | 58.375 |
| 4 | 5.380 | 11.404 | 92.042 |

Pi target: <25 us per 12-frame block for one instance at documented 1500 MHz, including transitions. **Unmeasured.**
`cpu_permille=6322` is a provisional target-based reservation: the current conversion gives mean100=1000 (10.00% core) and peak100=2500 (25.00%). Manifest max_voices=1. This does not enable or approve four-note polyphony. After Pi measurement, fit declared mean and peak to measured loads with 25% margin, then reassess the 20% instance-admission budget separately from the current runtime guard.

## Fixture metrics

RMS/DC cover the complete rendered fixture including silence. Spectral metrics average three Hann windows; transient-heavy and mostly silent fixtures need listening and time-resolved review as well.

| Fixture | Peak | RMS | DC | Nonfinite | Clamp samples | Energy above 18 kHz |
|---|---:|---:|---:|---:|---:|---:|
| default_metallic_pluck | 0.434323 | 0.054065 | -5.3e-10 | 0 | 0 | 0.0000% |
| inharmonic_bell | 0.420807 | 0.055415 | -1.2e-10 | 0 | 0 | 0.0000% |
| aggressive_bass | 0.554960 | 0.237905 | -2.41e-08 | 0 | 0 | 20.1342% |
| metallic_percussion | 0.402684 | 0.029024 | -2.42e-09 | 0 | 0 | 0.0000% |
| evolving_held_texture | 0.554417 | 0.189035 | 3.2e-09 | 0 | 0 | 7.9288% |
| morph_sweep | 0.435751 | 0.207931 | -3.89e-09 | 0 | 0 | 0.0000% |
| ratio_sweep | 0.445829 | 0.217022 | 2.82e-09 | 0 | 0 | 0.0000% |
| algorithm_changes | 0.441747 | 0.225918 | -1.07e-08 | 0 | 0 | 0.0000% |
| high_note_max_feedback | 0.435509 | 0.173204 | 2.11e-08 | 0 | 0 | 26.8278% |

## Aliasing/reference findings

At note 103, maximum FM/feedback, metallic ratios, zero BODY and DRIFT, the normalized spectral total-variation distances are below. Zero means matching normalized spectra; one means disjoint energy distributions. These are **spectral differences**, not isolated alias-energy measurements.

| Algorithm | Native vs 8x | 8x vs 16x | 8x host-delay vs short-delay |
|---|---:|---:|---:|
| 0 | 0.5663 | 0.2007 | 0.1995 |
| 1 | 0.5736 | 0.1065 | 0.2376 |
| 2 | 0.3778 | 0.0460 | 0.0758 |
| 3 | 0.3513 | 0.3132 | 0.3646 |
| 4 | 0.5110 | 0.0455 | 0.1129 |
| 5 | 0.4809 | 0.2230 | 0.1819 |
| 6 | 0.4704 | 0.2351 | 0.1725 |
| 7 | 0.6290 | 0.1479 | 0.5320 |

Extreme settings differ substantially from the filtered references. Several 8×/16× feedback spectra also differ substantially, so those cases are not a converged aliasing oracle. No oversampling/quality mode was added to production and no sound mapping was changed to hide these results. Aggressive/high-note aliasing and subjective usefulness remain explicit experimental limitations.

## Remaining gates

- [x] Phase 1 contract and approved design.
- [x] Phase 2 playable STACK, adapter, lifecycle and native pipeline.
- [x] Phase 3 eight algorithms, 16 macros, smoothing, drift, transitions and recipes.
- [x] Phase 4 native checks/sanitizers, nine fixtures, reference comparison, AArch64 build and exact-package Mac execution.
- [!] Installed Sidecar version unknown: compatibility assumption, not confirmation.
- [ ] Listening review of default character, macro usefulness, loudness and harsh extremes.
- [ ] Separately authorized Pi 4B validation at documented clock/thermal settings.
- [ ] One/two/four instances per track, four host voices per render core and whole-engine realistic-load measurement.
- [ ] Measured CPU declaration, polyphony decision and installation/rollback rehearsal.
- [ ] Hardware/release approval. No upload, SD-card edit, installed-plugin replacement, Rytm flash, push or PR is authorized by these native results.

File-preset recall without a selector remains unavailable in this API 2 host. The approved 16-control layout is retained; standard preset text files are parameter recipes, not an implemented preset browser.
