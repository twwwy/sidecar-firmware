# FM4 0.1.1 — four-instance capability, timing candidate

**Four-note playback is not enabled yet.** This candidate opts into the existing API 2 host instance pool (`max_voices=4`) and optimizes the DSP. The conservative CPU declaration is unchanged at 6322, so the current host admits only one instance. Do not describe this package as working four-note polyphony.

Stable ID `fm4.pm4`; version 0.1.1; compatible-from 0.1.0. All 16 parameters, defaults, ratio sets, routing, envelopes and macro curves retain the approved 0.1.0 mapping. No host/SDK/loader changes and no installation occurred.

## What changed

- Compile the eight fixed graphs directly instead of evaluating a dense 4×4 modulation matrix every sample. Only real edges interpolate coefficients, preserving source summation order.
- Update only moving parameter/ratio smoothers. VEL/GATE remain note-latched.
- Cache envelope coefficients until the relevant time parameter changes. No shared mutable state, allocation, locks or extra controls.
- State is 1220 bytes (formerly 1200), including masks and coefficient caches. Package image 36864 bytes + 8 B BSS. Four independent states require 4880 bytes of plugin payload; the host still allocates its own fixed-size state slots.

## Evidence

- Native checks and ASan/UBSan passed, including all eight routing equation references, lifecycle, envelopes, retrigger, bounded algorithm transitions, block-partition invariance, voice independence and randomized sweeps.
- Exact AArch64 package passed the repository loader/manifest checks and executed four independent states on Apple silicon. This is not Pi performance or host polyphonic playback evidence.
- `tests/compare.py` compared 960,000 samples with the preserved native 0.1.0 library across all algorithms, high modulation/feedback and live macro/envelope changes. Zero differing samples on this Mac/toolchain. This is a same-platform regression comparison, not a cross-platform golden guarantee.
- `make admission` compiles the actual `core/inst_alloc.cpp`: requested 1, 2, 4 each admits 1 with cpu_permille=6322. The test explicitly preserves this guard.

Same-machine native timings, mean of the eight per-algorithm means for one instance, microseconds per 12-frame call:

| Scenario | 0.1.0 | 0.1.1 optimized | Reduction |
|---|---:|---:|---:|
| default | 1.370 | 0.952 | 30.5% |
| extreme | 1.362 | 0.873 | 35.9% |
| changes | 2.840 | 1.389 | 51.1% |

These are native regression measurements, not a conversion to Pi timing. Current optimized Pi cost is unknown. The earlier ~8.3 us Pi observation belongs to the exact 0.1.0 package and is not transferred to this build.

## Why the host still admits one

Host declaration mean100=round(cpu_permille×0.1582), peak100=floor(mean100×2.5), fit=max(1,floor(2000/peak100)−1). At 6322, mean100=1000 and peak100=2500. Four admitted instances need peak100<=400 (mean about 4 us per instance at 48 kHz). Two need peak100<=666 (mean about 6.65 us). A 25% engineering margin makes the corresponding measured-mean goals approximately 3.2 us / 5.32 us. Peak/transition behavior must also fit; means alone do not establish it.

Extended stress qualification remains pending. The next necessary step is a brief Pi timing measurement of this exact optimized candidate, with a controlled held note and an algorithm-change segment. Do not lower cpu_permille merely to pass admission. A measurement-supported experimental declaration can then be chosen, potentially for two voices first if four do not fit. No SDK or host budget bypass is proposed.

## Reviewable artifacts and deployment boundary

The following artifacts are local, git-ignored build outputs, not downloads included in this source branch. Use the build commands below to produce a package.

- Timing candidate: [fm4-0.1.1.a64.rmp](build/candidates/fm4-0.1.1.a64.rmp), SHA-256 `12844b5cf1cac97d089f083a1359629a95a20ca252a9598fac9ac34f2a0fdaaa`.
- Preserved tested mono build: [fm4-0.1.0.a64.rmp](build/candidates/fm4-0.1.0.a64.rmp), SHA-256 `505324db00f6f652f419ba6ad6e547b83a132570a1bc5c159ff4cb354f66f28f`.

Separate approval is required to upload the candidate alongside the installed 0.1.0 package and temporarily assign U5 for timing. Preserve the previous assignment and restore it after the measurement if requested or if the candidate fails. Do not overwrite the installed mono package or reuse its hardware result for the new hash.

Reproduce local checks/build:

```sh
make -C plugins/fm4 native admission sanitize
# From repository root, with the existing toolchain image:
docker run --rm -v "$PWD:/work" -w /work rytm-circle make -C plugins/fm4 a64 PY=python3
make -C plugins/fm4 check package-check
python3 plugins/fm4/tests/compare.py plugins/fm4/build/baseline-0.1.0/libfm4.dylib
```

The comparison requires the locally preserved baseline library; fresh checkouts still have the independent equation tests. Old fixture reports and HARDWARE.md remain evidence for 0.1.0 only. The historical `make report` generator refuses to relabel them as 0.1.1 evidence.

- [x] Four-instance capability flag, compatible version bump, DSP optimization.
- [x] Native/sanitizer, exact-package Mac execution, regression comparison and actual admission check.
- [x] Versioned candidate and mono rollback artifact prepared.
- [!] Effective polyphony blocked by the declared admission cost pending optimized-build Pi timing.
- [ ] Approved upload/temporary assignment and short timing observation.
- [ ] Evidence-supported CPU declaration, admitted multi-note playback and user assessment.
