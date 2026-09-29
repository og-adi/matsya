/*
 * waveform_gen.h
 * ---------------------------------------------------------------------------
 * Portable core: adaptive parameter mapping + LUT fill.
 * Deliberately has ZERO STM32/HAL dependencies so you can compile and
 * unit-test this file on your laptop (gcc) before it ever touches hardware.
 * This is the direct C port of adaptive_params()/generate_lfm_chirp() from
 * sonar_sim.py -- keep the two in sync if you tune constants.
 */

#ifndef WAVEFORM_GEN_H
#define WAVEFORM_GEN_H

#include <stdint.h>
#include <stddef.h>

#define SONAR_LUT_MAX_SAMPLES   4096u   /* size your DMA buffer to fit in SRAM;
                                            4096 * 2 bytes = 8 KB, fine on F4 */
#define DAC_FULL_SCALE          4095u   /* 12-bit DAC */
#define DAC_MID_SCALE           2048u

typedef struct {
    float f_start_hz;
    float f_end_hz;
    float duration_s;
    float amplitude;      /* 0.0 .. 1.0 */
    float difficulty;     /* 0.0 .. 1.0, diagnostic/debug only */
} sonar_params_t;

typedef enum {
    WINDOW_RECT = 0,
    WINDOW_HAMMING,
    WINDOW_HANN,
    WINDOW_BLACKMAN
} window_type_t;

typedef enum {
    MOD_LFM = 0,       /* Linear Frequency Modulated chirp -- general purpose */
    MOD_GEOMETRIC,     /* Logarithmic sweep -- biases energy toward low end */
    MOD_PHASE_CODED    /* Fixed-carrier Barker-7 coded pulse -- pulse compression */
} modulation_type_t;

/* Maps normalized environmental inputs to waveform parameters.
 * turbidity: 0.0 (clear) .. 1.0 (very muddy)
 * depth_m:   0 .. 200 (clamped internally)
 * Mirrors adaptive_params() in sonar_sim.py -- keep constants identical. */
sonar_params_t sonar_adaptive_params(float turbidity, float depth_m);

/* Fills `out` with `n_samples` of a 12-bit-DAC-ready LFM chirp, windowed.
 * Returns the number of samples actually written (<= SONAR_LUT_MAX_SAMPLES).
 * Call this from your main loop / low-priority task when adaptive_params()
 * output has changed beyond hysteresis -- NEVER call this from inside the
 * DMA/timer ISR path.
 */
uint32_t sonar_fill_lfm_lut(uint16_t *out, uint32_t max_samples,
                             const sonar_params_t *params,
                             float sample_rate_hz,
                             window_type_t window);

/* Geometric (logarithmic) sweep -- same fill-then-silence-pad behavior as
 * sonar_fill_lfm_lut(). Frequency grows exponentially rather than linearly:
 * spends more time near f_start, accelerates toward f_end. */
uint32_t sonar_fill_geometric_lut(uint16_t *out, uint32_t max_samples,
                                   const sonar_params_t *params,
                                   float sample_rate_hz,
                                   window_type_t window);

/* Phase-coded pulse (Barker-7) -- fixed carrier at (f_start+f_end)/2, with
 * 180-degree phase flips at the 7 chip boundaries. Same fill-then-silence-
 * pad behavior as the other two generators. */
uint32_t sonar_fill_phase_coded_lut(uint16_t *out, uint32_t max_samples,
                                     const sonar_params_t *params,
                                     float sample_rate_hz,
                                     window_type_t window);

/* Unified dispatcher -- call this one function and switch modulation types
 * by changing `mod` alone. This is what makes the modulation type
 * selectable "on the fly" from a single call site in main.c. */
uint32_t sonar_fill_waveform(uint16_t *out, uint32_t max_samples,
                              const sonar_params_t *params,
                              float sample_rate_hz,
                              window_type_t window,
                              modulation_type_t mod);

#endif /* WAVEFORM_GEN_H */
