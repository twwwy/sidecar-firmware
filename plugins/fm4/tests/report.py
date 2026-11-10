#!/usr/bin/env python3
"""Publish measured local evidence after running documented validation commands. MIT."""
import csv, hashlib, json, pathlib, platform, subprocess, sys
from render import HERE, source_identity
sys.path.insert(0,str(HERE.parents[1]/'tools'))
import rmp_pack
if json.loads((HERE/'manifest.json').read_text())['version']!='0.1.0':
    raise SystemExit('This historical report generator is for 0.1.0; see POLYPHONY.md for 0.1.1 evidence. Do not reuse old fixture/timing results.')
identity=source_identity()
artifact=HERE/'fm4.a64.rmp'
data=artifact.read_bytes();header=rmp_pack.parse_header(data)
metrics=json.loads((HERE/'build/fixtures/metrics.json').read_text())
comparison=json.loads((HERE/'build/reference/comparison.json').read_text())
# Require fixture/reference custody to match current actual source, not just git HEAD.
for fixture in (HERE/'build/fixtures').glob('*.json'):
    if fixture.name=='metrics.json':continue
    assert json.loads(fixture.read_text())['identity']==identity, f'stale fixture: {fixture.name}'
assert comparison['identity']==identity,'stale reference run'
rows=list(csv.DictReader((HERE/'build/timing.csv').open()))
receipt=dict(identity=identity,artifact_sha256=hashlib.sha256(data).hexdigest(),artifact_bytes=len(data),
             header=header,host=platform.platform(),cpu=subprocess.check_output(['sysctl','-n','machdep.cpu.brand_string'],text=True).strip(),
             native_compiler=subprocess.check_output(['cc','--version'],text=True).splitlines()[0],
             aarch64_compiler='Arm GNU Toolchain 13.3.rel1 aarch64-none-elf-gcc',
             toolchain_image='sha256:6e4e2aaf23574a5b8621fb1dd1c36d4d300925936039a124dde12c5f65d920ea')
(HERE/'build/identity.json').write_text(json.dumps(receipt,indent=2)+'\n')
lines=['# FM4 local validation report','',
'Candidate only. Implemented, tested natively and built for Pi; **not tested on Pi**. Subjective listening is pending. No deployment or publication occurred.','',
f"Base revision: `{identity['base_revision']}`. The implementation is an uncommitted addition; the base revision alone does not identify it.",
f"Actual implementation source-set SHA-256: `{identity['source_sha256']}`. Individual source hashes and package/toolchain custody are in `build/identity.json`.",
f"Package SHA-256: `{receipt['artifact_sha256']}` ({len(data)} bytes).",'',
'## Scope and evidence','',
'- `make check`: explicit routing references for all eight algorithms; API lifecycle; 44.1/48/96 kHz; envelope timing; velocity/endpoints; repeated notes; rapid parameter changes; 20,000 randomized blocks; latest-target algorithm switching; exact retrigger fade shape; deterministic block-partition invariance; independent interleaved states; finite/bounded output; level-zero and post-release silence.',
'- `make sanitize`: same suite under AddressSanitizer and UndefinedBehaviorSanitizer, passed.',
'- Clean A4 pitch error approximately −0.00070 cents in the native test (tolerance 1 cent). Maximum absolute sustained-test DC approximately 0.00022693 (threshold 0.001). Random-stress clamp count zero.',
'- `make package-check`: repository loader validates target, CRC and manifest, relocates the exact RMPA image and executes all eight algorithms with four independent states on this Apple-silicon Mac. Also rejects wrong-target and corrupt-CRC packages. Not a host admission test or Pi test.',
'- Nine native fixtures generated at 48 kHz, 12-frame blocks, seed 0x464d3401, velocity 0.85; parameters, note/off timeline, source hashes and WAV hash accompany each.',
'- Eight extreme high-note algorithm comparisons against offline 8× and 16× references, with preserved host-time feedback delay; separate short-delay variants measure delay sensitivity.', '',
'## Memory and package','',
'Production instance state: **1,200 bytes**, including both transition banks and diagnostics. Below the 4 KiB FM4 assertion and 65,536-byte Pi limit. No render-time allocation.',
f"Package image: {header['image_size']} bytes; BSS: {header['bss_size']} bytes; relocations: {header['reloc_count']}. Immutable math tables hold 4097 sine floats and 1025 fractional-exp2 floats (20,488 bytes). Shared writable BSS is only initialized host configuration; all voice state is separate.", '',
'## Native timing — regression evidence only','',
f"Host: {receipt['cpu']}, {receipt['host']}; {receipt['native_compiler']}, `-O2`. The Mac CPU clock was not fixed or measured. Timings do not prove Pi headroom.",
'Each row averages 10,000 timed 12-frame groups after 1,000 warm-up groups. Covers all algorithms, defaults, high-note maximum FM/feedback/drift, and frequent parameter/algorithm changes. Groups render states serially; this is not the host mixer or whole-engine load. Individual observed maxima include OS scheduling and instrumentation disturbances.','',
'| Independent states per group | Minimum scenario mean (us) | Maximum scenario mean (us) | Largest observed group (us) |',
'|---:|---:|---:|---:|']
for n in [1,2,4]:
    rr=[r for r in rows if int(r['instances'])==n]
    means=[float(r['mean_ns_per_12_frame_group'])/1000 for r in rr]
    peaks=[float(r['max_observed_ns_per_group'])/1000 for r in rr]
    lines.append(f'| {n} | {min(means):.3f} | {max(means):.3f} | {max(peaks):.3f} |')
lines += ['',
'Pi target: <25 us per 12-frame block for one instance at documented 1500 MHz, including transitions. **Unmeasured.**',
'`cpu_permille=6322` is a provisional target-based reservation: the current conversion gives mean100=1000 (10.00% core) and peak100=2500 (25.00%). Manifest max_voices=1. This does not enable or approve four-note polyphony. After Pi measurement, fit declared mean and peak to measured loads with 25% margin, then reassess the 20% instance-admission budget separately from the current runtime guard.', '',
'## Fixture metrics','',
'RMS/DC cover the complete rendered fixture including silence. Spectral metrics average three Hann windows; transient-heavy and mostly silent fixtures need listening and time-resolved review as well.','',
'| Fixture | Peak | RMS | DC | Nonfinite | Clamp samples | Energy above 18 kHz |',
'|---|---:|---:|---:|---:|---:|---:|']
for name,m in metrics.items():lines.append(f"| {name} | {m['peak']:.6f} | {m['rms']:.6f} | {m['dc']:.3g} | {m['nonfinite']} | {m['clamp_samples']} | {m['above_18khz_energy_fraction']:.4%} |")
lines += ['', '## Aliasing/reference findings','',
'At note 103, maximum FM/feedback, metallic ratios, zero BODY and DRIFT, the normalized spectral total-variation distances are below. Zero means matching normalized spectra; one means disjoint energy distributions. These are **spectral differences**, not isolated alias-energy measurements.','',
'| Algorithm | Native vs 8x | 8x vs 16x | 8x host-delay vs short-delay |','|---|---:|---:|---:|']
for row in comparison['results']:
    lines.append(f"| {row['algorithm']} | {row['spectral_total_variation_native_vs_8x']:.4f} | {row['spectral_total_variation_8x_vs_16x']:.4f} | {row['spectral_total_variation_delay_change']:.4f} |")
lines += ['',
'Extreme settings differ substantially from the filtered references. Several 8×/16× feedback spectra also differ substantially, so those cases are not a converged aliasing oracle. No oversampling/quality mode was added to production and no sound mapping was changed to hide these results. Aggressive/high-note aliasing and subjective usefulness remain explicit experimental limitations.', '',
'## Remaining gates','',
'- [x] Phase 1 contract and approved design.',
'- [x] Phase 2 playable STACK, adapter, lifecycle and native pipeline.',
'- [x] Phase 3 eight algorithms, 16 macros, smoothing, drift, transitions and recipes.',
'- [x] Phase 4 native checks/sanitizers, nine fixtures, reference comparison, AArch64 build and exact-package Mac execution.',
'- [!] Installed Sidecar version unknown: compatibility assumption, not confirmation.',
'- [ ] Listening review of default character, macro usefulness, loudness and harsh extremes.',
'- [ ] Separately authorized Pi 4B validation at documented clock/thermal settings.',
'- [ ] One/two/four instances per track, four host voices per render core and whole-engine realistic-load measurement.',
'- [ ] Measured CPU declaration, polyphony decision and installation/rollback rehearsal.',
'- [ ] Hardware/release approval. No upload, SD-card edit, installed-plugin replacement, Rytm flash, push or PR is authorized by these native results.', '',
'File-preset recall without a selector remains unavailable in this API 2 host. The approved 16-control layout is retained; standard preset text files are parameter recipes, not an implemented preset browser.', '']
(HERE/'VALIDATION.md').write_text('\n'.join(lines))
print('Wrote VALIDATION.md and build/identity.json; source and fixture identities agree')
