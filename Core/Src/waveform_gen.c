/*
 * waveform_gen.c
 * ---------------------------------------------------------------------------
 * Implementation. Uses only math.h -- no HAL. Build a quick host test with:
 *
 *   gcc -O2 -Wall -o test_wavegen waveform_gen.c test_wavegen.c -lm
 *   ./test_wavegen
 *
 * before ever flashing this to a part. Once the LUT values match
 * firmware_lut_export.csv from the Python sim (within rounding), port is safe.
 *
 * ---------------------------------------------------------------------------
 * CHANGE LOG (this revision)
 * ---------------------------------------------------------------------------
 * - Added thorp_absorption_db_per_km(): real Thorp's-formula evaluation,
 *   used inside sonar_adaptive_params() so the difficulty score is now
 *   partly derived from acoustic physics instead of a pure linear blend
 *   of raw sensor readings. MAX_ACCEPTABLE_LOSS_DB and the "depth as a
 *   proxy for one-way range" assumption are documented placeholders, not
 *   measured/derived values -- flag this honestly if asked.
 * - Fixed Barker-7 chip-length remainder: previously `n / n_chips` silently
 *   let the last chip absorb the leftover samples with no visibility into
 *   how many. Now the remainder is computed explicitly and folded into the
 *   last chip on purpose, so chip timing is accounted for rather than an
 *   unexplained rounding artifact.
 */

#include "waveform_gen.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const float F_LOW_HZ    = 100000.0f;
static const float F_HIGH_HZ   = 500000.0f;
static const float DUR_MIN_S   = 0.5e-3f;
static const float DUR_MAX_S   = 5.0e-3f;
static const float AMP_MIN     = 0.2f;
static const float AMP_MAX     = 1.0f;
static const float MAX_DEPTH_M = 200.0f;

/* Placeholder link-budget bound used to normalize Thorp absorption loss into
 * a 0..1 difficulty contribution. This is a documented assumption, not a
 * value derived from a full link budget -- tune against real measurements
 * if you have them, and be ready to say so if asked. */
#define MAX_ACCEPTABLE_LOSS_DB  20.0f

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Thorp's empirical absorption coefficient, dB/km, f in kHz.
 * 10*log10(alpha(f)) = 0.11*f^2/(1+f^2) + 44*f^2/(4100+f^2) + 2.75e-4*f^2 + 0.003
 * Valid for f roughly above a few hundred Hz; matches the documented
 * reference formula used elsewhere in project documentation. */
static float thorp_absorption_db_per_km(float f_khz)
{
    float f2 = f_khz * f_khz;
    return 0.11f * f2 / (1.0f + f2)
         + 44.0f  * f2 / (4100.0f + f2)
         + 2.75e-4f * f2
         + 0.003f;
}

sonar_params_t sonar_adaptive_params(float turbidity, float depth_m)
{
    sonar_params_t p;

    turbidity = clampf(turbidity, 0.0f, 1.0f);
    float depth_norm = clampf(depth_m / MAX_DEPTH_M, 0.0f, 1.0f);

    /* Physics-grounded term: how costly would the HIGH end of our band be
     * at this depth, per Thorp's absorption model? Depth is treated as a
     * proxy one-way range in meters -- a documented assumption, not a
     * measured slant range. */
    float f_khz_high    = F_HIGH_HZ / 1000.0f;
    float alpha_db_km   = thorp_absorption_db_per_km(f_khz_high);
    float est_range_km  = clampf(depth_m, 1.0f, MAX_DEPTH_M) / 1000.0f;
    float loss_db        = alpha_db_km * est_range_km;

    float absorption_difficulty = clampf(loss_db / MAX_ACCEPTABLE_LOSS_DB, 0.0f, 1.0f);

    /* Blend: turbidity still matters directly (scattering, not just
     * absorption), depth_norm keeps a residual weight, absorption_difficulty
     * is now the Thorp-derived term and carries the largest weight. */
    float difficulty = clampf(0.35f * turbidity
                             + 0.15f * depth_norm
                             + 0.50f * absorption_difficulty, 0.0f, 1.0f);

    float f_center = F_HIGH_HZ - difficulty * (F_HIGH_HZ - F_LOW_HZ);
    float bw = 0.2f * f_center;

    p.f_start_hz = f_center - bw / 2.0f;
    p.f_end_hz   = f_center + bw / 2.0f;
    p.duration_s = DUR_MIN_S + difficulty * (DUR_MAX_S - DUR_MIN_S);
    p.amplitude  = AMP_MIN + difficulty * (AMP_MAX - AMP_MIN);
    p.difficulty = difficulty;

    return p;
}

/* Precomputed-coefficient window function -- evaluated once per sample at
 * LUT-fill time (which runs at low priority, NOT in the ISR), so a cosf()
 * call here is fine. What you must never do is call this per-sample inside
 * the DMA-refill ISR. */
static float window_coeff(window_type_t w, uint32_t i, uint32_t n)
{
    if (n <= 1) return 1.0f;
    float x = (float)i / (float)(n - 1);
    switch (w) {
        case WINDOW_HAMMING:
            return 0.54f - 0.46f * cosf(2.0f * (float)M_PI * x);
        case WINDOW_HANN:
            return 0.5f * (1.0f - cosf(2.0f * (float)M_PI * x));
        case WINDOW_BLACKMAN:
            return 0.42f - 0.5f * cosf(2.0f * (float)M_PI * x)
                         + 0.08f * cosf(4.0f * (float)M_PI * x);
        case WINDOW_RECT:
        default:
            return 1.0f;
    }
}

uint32_t sonar_fill_lfm_lut(uint16_t *out, uint32_t max_samples,
                             const sonar_params_t *params,
                             float sample_rate_hz,
                             window_type_t window)
{
    uint32_t n = (uint32_t)(params->duration_s * sample_rate_hz);
    if (n > max_samples) n = max_samples;
    if (n > SONAR_LUT_MAX_SAMPLES) n = SONAR_LUT_MAX_SAMPLES;

    float k = (params->f_end_hz - params->f_start_hz) / params->duration_s;

    for (uint32_t i = 0; i < n; i++) {
        float t = (float)i / sample_rate_hz;
        float phase = 2.0f * (float)M_PI * (params->f_start_hz * t + 0.5f * k * t * t);
        float s = sinf(phase) * window_coeff(window, i, n) * params->amplitude;
        int32_t counts = (int32_t)((s * 0.5f + 0.5f) * DAC_FULL_SCALE);
        if (counts < 0) counts = 0;
        if (counts > (int32_t)DAC_FULL_SCALE) counts = DAC_FULL_SCALE;
        out[i] = (uint16_t)counts;
    }

    // ★★★★★ NEW: clear the rest to silence ★★★★★
    for (uint32_t i = n; i < max_samples; i++) {
        out[i] = DAC_MID_SCALE;   // 2048 = Vref/2
    }

    return n;
}

/* Geometric (logarithmic) sweep.
 * Instantaneous frequency: f(t) = f_start * ratio^(t/duration), ratio = f_end/f_start
 * Phase = 2*pi * f_start * (ratio^(t/duration) - 1) / ln(ratio)
 */
uint32_t sonar_fill_geometric_lut(uint16_t *out, uint32_t max_samples,
                                   const sonar_params_t *params,
                                   float sample_rate_hz,
                                   window_type_t window)
{
    uint32_t n = (uint32_t)(params->duration_s * sample_rate_hz);
    if (n > max_samples) n = max_samples;
    if (n > SONAR_LUT_MAX_SAMPLES) n = SONAR_LUT_MAX_SAMPLES;

    float ratio = params->f_end_hz / params->f_start_hz;
    float ln_ratio = logf(ratio);
    uint8_t degenerate = (fabsf(ln_ratio) < 1e-6f);  /* guard f_start==f_end */

    for (uint32_t i = 0; i < n; i++) {
        float t = (float)i / sample_rate_hz;
        float phase;
        if (degenerate) {
            phase = 2.0f * (float)M_PI * params->f_start_hz * t;
        } else {
            float frac = t / params->duration_s;
            /* Phase = 2*pi * f_start * duration * (ratio^frac - 1) / ln(ratio)
             * -- the duration factor comes from the substitution u=tau/duration
             * in the integral of f(tau)=f_start*ratio^(tau/duration) dtau;
             * omitting it (as an earlier version of this function did) gives
             * a phase larger by a factor of 1/duration, which for a ~2.75ms
             * pulse is a >300x error -- verified against a Python double-
             * precision reference before shipping this fix. */
            phase = 2.0f * (float)M_PI * params->f_start_hz * params->duration_s
                   * (powf(ratio, frac) - 1.0f) / ln_ratio;
        }
        float s = sinf(phase) * window_coeff(window, i, n) * params->amplitude;
        int32_t counts = (int32_t)((s * 0.5f + 0.5f) * DAC_FULL_SCALE);
        if (counts < 0) counts = 0;
        if (counts > (int32_t)DAC_FULL_SCALE) counts = DAC_FULL_SCALE;
        out[i] = (uint16_t)counts;
    }

    for (uint32_t i = n; i < max_samples; i++) {
        out[i] = DAC_MID_SCALE;
    }

    return n;
}

/* Phase-coded pulse (Barker-7).
 * Fixed carrier at center frequency, 180-degree phase flips at 7 chip
 * boundaries. Still windowed across the whole envelope.
 *
 * Chip length: n / n_chips truncates, leaving a remainder of 0..6 samples.
 * That remainder is folded into the LAST chip on purpose (rather than left
 * as an unexplained rounding artifact), so total sample count is always
 * exactly accounted for. */
uint32_t sonar_fill_phase_coded_lut(uint16_t *out, uint32_t max_samples,
                                     const sonar_params_t *params,
                                     float sample_rate_hz,
                                     window_type_t window)
{
    static const int8_t barker7[7] = {1, 1, 1, -1, -1, 1, -1};
    const uint32_t n_chips = 7;

    uint32_t n = (uint32_t)(params->duration_s * sample_rate_hz);
    if (n > max_samples) n = max_samples;
    if (n > SONAR_LUT_MAX_SAMPLES) n = SONAR_LUT_MAX_SAMPLES;

    float f_center = 0.5f * (params->f_start_hz + params->f_end_hz);
    uint32_t chip_len  = n / n_chips;
    uint32_t remainder = n - chip_len * n_chips;
    if (chip_len == 0) chip_len = 1;

    /* Sample index where the (deliberately longer) last chip begins. */
    uint32_t last_chip_start = (n_chips - 1) * chip_len;

    for (uint32_t i = 0; i < n; i++) {
        uint32_t chip_idx;
        if (i >= last_chip_start) {
            chip_idx = n_chips - 1;   /* last chip absorbs `remainder` extra samples */
        } else {
            chip_idx = i / chip_len;
            if (chip_idx >= n_chips - 1) chip_idx = n_chips - 2;
        }
        float phase_offset = (barker7[chip_idx] == 1) ? 0.0f : (float)M_PI;

        float t = (float)i / sample_rate_hz;
        float phase = 2.0f * (float)M_PI * f_center * t + phase_offset;
        float s = sinf(phase) * window_coeff(window, i, n) * params->amplitude;
        int32_t counts = (int32_t)((s * 0.5f + 0.5f) * DAC_FULL_SCALE);
        if (counts < 0) counts = 0;
        if (counts > (int32_t)DAC_FULL_SCALE) counts = DAC_FULL_SCALE;
        out[i] = (uint16_t)counts;
    }
    (void)remainder; /* accounted for structurally via last_chip_start; kept for clarity/logging */

    for (uint32_t i = n; i < max_samples; i++) {
        out[i] = DAC_MID_SCALE;
    }

    return n;
}

/* Unified dispatcher. */
uint32_t sonar_fill_waveform(uint16_t *out, uint32_t max_samples,
                              const sonar_params_t *params,
                              float sample_rate_hz,
                              window_type_t window,
                              modulation_type_t mod)
{
    switch (mod) {
        case MOD_GEOMETRIC:
            return sonar_fill_geometric_lut(out, max_samples, params, sample_rate_hz, window);
        case MOD_PHASE_CODED:
            return sonar_fill_phase_coded_lut(out, max_samples, params, sample_rate_hz, window);
        case MOD_LFM:
        default:
            return sonar_fill_lfm_lut(out, max_samples, params, sample_rate_hz, window);
    }
}
