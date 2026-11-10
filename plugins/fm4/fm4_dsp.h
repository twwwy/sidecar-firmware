// Original FM4 phase-modulation instrument. SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FM4_PARAMS 16
#define FM4_SEED 0x464d3401u
#ifndef FM4_CPU_PERMILLE
// Experimental fail-conservative reservation, NOT a measurement of the optimized build.
// max_voices=4 opts in to instancing; this reservation still admits only ONE.
// 6322 maps to 10.00% mean / 25.00% peak in the current Pi admission model.
#define FM4_CPU_PERMILLE 6322u
#endif
enum { ALGO, RAT, COL, FM, FDBK, SWP, DEC, MRPH, ATK, REL, BODY, DRFT, VEL, TUNE, GATE, LEV };
typedef struct {
    uint8_t current[4], delayed[4], carriers, anchor;
} fm4_route;
extern const fm4_route fm4_routes[8];
extern const float fm4_defaults[16];
typedef struct {
    float phase[4], previous[4];
    float edge[4][4], feedback[4][4], weight[4];
    float edge_step[4][4], feedback_step[4][4], weight_step[4];
#ifdef FM4_REFERENCE_FACTOR
    float delay[FM4_REFERENCE_FACTOR][4];
    unsigned delay_position;
#endif
    int algorithm;
} fm4_bank;
typedef struct {
    uint32_t rng, position, length;
    float from, to, value;
} fm4_drift;
typedef struct {
    float rate, dc_r;
    float raw[16], target[16], smooth[16], slew[16];
    float ratio_target[4], ratio[4], inc[4], inc_step[4];
    float note, velocity, timbre, contour, amplitude, attack_step, body_keep, release_keep, contour_keep;
    float dc_x, dc_y, pending_note, pending_velocity, release_progress;
    float cached_atk, cached_dec, cached_rel;
    uint32_t smooth_mask, ratio_mask;
    fm4_bank bank[2];
    fm4_drift drift[4];
    uint32_t seed, note_count, control_phase, stage_samples;
    uint32_t transition, transition_length, retrigger, retrigger_length;
    float pre_peak, post_peak;
    uint32_t clamps, nonfinite;
    int stage, gated, held, pending_held, active_bank, requested_algorithm;
} fm4_state;
int fm4_init(fm4_state *s, float rate, uint32_t seed);
void fm4_params(fm4_state *s, const float p[16]);
void fm4_note_on(fm4_state *s, float note, float velocity);
void fm4_note_off(fm4_state *s);
void fm4_leave(fm4_state *s);
void fm4_stats(const fm4_state *s, float peaks[2], uint32_t counters[2]);
void fm4_render(fm4_state *s, float *out, int32_t n);
// Pure helpers exported for independent numerical/routing tests.
float fm4_sin(float radians);
float fm4_exp2(float x);
void fm4_ratios(float raw, float log2_ratios[4]);
float fm4_bank_tick(fm4_bank *b, const float increments[4], float index, float activity);
#ifdef __cplusplus
}
#endif
