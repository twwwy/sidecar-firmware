# FM4 — experimental four-operator phase modulation

FM4 is an original mono-output RMP API 2 user machine for Raspberry Pi Sidecar. The interface has exactly 16 controls, two parameter pages, eight routings, deterministic drift and bounded algorithm transitions. It is inspired by musical macro control, not a model of a particular commercial synthesizer.

**Status:** 0.1.1 is an optimized candidate with four-instance capability declared, but the unchanged CPU reservation still admits one instance. Effective polyphony awaits a short Pi timing measurement; see [POLYPHONY.md](POLYPHONY.md). The initial stable hardware observation in [HARDWARE.md](HARDWARE.md) applies only to 0.1.0.

Stable ID: `fm4.pm4`. Version: `0.1.1`. Compatible-from: `0.1.0`.
Target source: `c442c6db407bf01a51530e82deac23308eeb5a67` (`v0.4.3-1-gc442c6d`, VERSION 0.4.3). The connected Pi reports 0.4.3, commit `1debc00`; 0.1.0 was observed there. The 0.1.1 candidate has not been tested on Pi. No API 3 features are used.

## Build and native checks

Run from the repository root. Native tools: Apple clang, make, Python 3 standard library. No Python dependencies are required.

```sh
make -C plugins/fm4 check sanitize
make -C plugins/fm4 render
make -C plugins/fm4 bench
make -C plugins/fm4 aliasing
```

`check` validates DSP behavior and the real packer manifest; if a package exists it also validates/reconstructs that package. `sanitize` runs the DSP checks under ASan/UBSan. `render` writes nine WAVs, event/parameter metadata and metrics under `build/fixtures/`. `bench` writes `build/timing.csv`. `aliasing` builds offline 8×/16× reference variants and writes `build/reference/`; these variants are not shipped in the package.

The checked-in sine/exponential tables are generated with:

```sh
python3 plugins/fm4/tests/gen_tables.py
```

With `aarch64-none-elf-gcc` installed:

```sh
make -C plugins/fm4 a64 PY=python3
```

Without it, use the repository's toolchain image. The explicit quoted mount works when the checkout path contains spaces (the SDK's implicit Docker invocation does not quote its repository mount):

```sh
docker build -t rytm-circle pi/circle
docker run --rm -v "$PWD:/work" -w /work rytm-circle \
  make -C plugins/fm4 a64 PY=python3
```

This builds only FM4 and its SDK shims, not the kernel or Rytm firmware. Output: `plugins/fm4/fm4.a64.rmp`.

On an Apple-silicon Mac, the repository loader can execute those exact AArch64 bytes:

```sh
make -C plugins/fm4 package-check
```

This tests RMPA validation, relocation, manifest parsing, lifecycle and eight algorithms with four independent states. It is **Mac execution**, not Pi execution. `package-check` requires an already-built artifact and does not silently rebuild it.

## Controls

Values below are raw 0…127. Fractional inputs are accepted below 127. The host's list decoding uses `floor(value)*count >> 7`.

| P | Label | Default | Meaning |
|---|---|---:|---|
| 1 | ALGO | 0 | STACK, DUAL, FORK, FAN, SPLIT, CROSS, RING, PARA (16-value zones) |
| 2 | RAT | 72 | Curated harmonic, subharmonic, near-unison and metallic ratio families |
| 3 | COL | 64 | Modulation-stage and carrier spectral balance |
| 4 | FM | 52 | Nonlinear feed-forward modulation depth |
| 5 | FDBK | 24 | Delayed self/cross-feedback |
| 6 | SWP | 96 | Bipolar contour depth; 64 is neutral, negative opens upward |
| 7 | DEC | 64 | Modulation-contour decay and one-shot body duration |
| 8 | MRPH | 0 | Static coordinated ratio/coupling deformation |
| 9 | ATK | 0 | Amplitude attack |
| 10 | REL | 64 | Amplitude release |
| 11 | BODY | 72 | Tonal-anchor strength and carrier balance |
| 12 | DRFT | 8 | Slow deterministic pitch/ratio instability |
| 13 | VEL | 80 | Velocity-to-FM sensitivity; latched next note |
| 14 | TUNE | 64 | Fine tuning; exactly zero at 64 |
| 15 | GATE | 0 | 0…63 one-shot, 64…127 gated; latched next note |
| 16 | LEV | 100 | Output level |

See [DESIGN.md](DESIGN.md) for the frozen mathematical mapping and transition behavior. Changes that alter existing sounds require an explicit compatibility decision, not silent retuning under the same compatible version range.

## Presets and listening fixtures

`presets/*.txt` uses the existing generic format: sixteen `P<n> <integer>` lines. The fixture renderer loads the same parameter recipes; dynamic sweep event timelines are in fixture JSON, not hidden preset data.

**Verified limitation:** the API 2 host's file-preset recall requires a `text: "preset"` parameter. This exact 16-control layout has no such selector. These files are valid recipes, but placing them on the card does not make them selectable in FM4. Apply their values manually and save with the host instrument's existing sound workflow. No new browser, hidden selector or seventeenth control is implemented.

Fixtures: default metallic pluck, inharmonic bell, aggressive bass, metallic percussion, evolving held texture, MORPH sweep, RATIO sweep, sustained algorithm changes, and maximum-feedback high-note stress. Every fixture records the base revision plus a hash manifest of the actual source set, parameters/events, seed, note, velocity, block size, sample rate, audio hash and metrics. Generation is not listening approval. Names describe intended sound roles, not an assessed listening result.

## CPU, memory and aliasing

See [VALIDATION.md](VALIDATION.md) for observed evidence. The target is <25 microseconds per 12-frame block for one instance on a Pi 4B at documented 1500 MHz, including transition cases. Pi timing for the optimized 0.1.1 candidate remains pending; the earlier 0.1.0 observation is recorded in [HARDWARE.md](HARDWARE.md).

The candidate reserves `cpu_permille=6322`, explicitly **provisional and target-based**, not measured. In the target host's conversion it maps to a 10.00% mean / 25.00% inferred peak reservation, covering the 25 us engineering target with the host's 2.5× peak factor. It is not chosen to permit polyphony: the manifest advertises four-instance capability, but this cost still clamps actual admission to one instance. Replace it only with a defensible measurement-derived declaration after Pi qualification.

Production is 1× sample rate. Base frequencies cap at 0.20×Fs, which compresses ratios at high notes and does not eliminate sideband aliasing. Aggressive patches are intentionally capable of noisy output. The 8×/16× comparisons show large spectral differences for extreme settings and incomplete convergence in feedback cases. No alias-free claim is made, and no quality switch consumes a control.

No plugin heap allocation, locks, logging or I/O occurs in callbacks. All mutable synthesis state is per instance. The only mutable image-wide value is the host sample rate, assigned in `init` before callbacks and then read-only.

## Installation and rollback — separately authorized hardware step

Before deploying, identify installed Sidecar revision, record package SHA-256, back up the exact slot assignment (ID/version), previous package, and project slot map. Preserve the prior package rather than overwriting it.

1. After explicit hardware approval, upload/copy the candidate as `/Plugins/fm4.pm4-0.1.1.rmp` using the supported User machines upload or card workflow.
2. Rescan and assign the intended U1…U5 slot. Check ID, actual version, API and reported state size. Do not infer compatibility from the file name.
3. Test the exact candidate on one voice before attempting wider load. Retain logs and package hash.
4. To roll back, stop the test and restore the recorded slot assignment to the exact prior package/version (retain/install that prior package if necessary). If the slot was empty, clear only that slot. Verify the restored identity before playing.

The host may choose the newest compatible package when an exact requested version is unavailable. Deleting a candidate alone is not a reliable rollback. No kernel rebuild, SD reimage or Rytm flash is part of FM4 installation. The 0.1.1 candidate has not been installed on the Pi.
