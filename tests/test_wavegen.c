#include "waveform_gen.h"

#include <assert.h>
#include <stdio.h>

static void test_adaptive_parameter_bounds(void)
{
    sonar_params_t low = sonar_adaptive_params(-1.0f, -10.0f);
    sonar_params_t high = sonar_adaptive_params(2.0f, 300.0f);

    assert(low.difficulty >= 0.0f && low.difficulty <= 1.0f);
    assert(high.difficulty >= 0.0f && high.difficulty <= 1.0f);
    assert(low.f_start_hz >= 100000.0f && low.f_end_hz <= 500000.0f);
    assert(high.f_start_hz >= 100000.0f && high.f_end_hz <= 500000.0f);
    assert(low.duration_s >= 0.0005f && low.duration_s <= 0.005f);
    assert(high.duration_s >= 0.0005f && high.duration_s <= 0.005f);
    assert(low.amplitude >= 0.2f && low.amplitude <= 1.0f);
    assert(high.amplitude >= 0.2f && high.amplitude <= 1.0f);
}

static void test_waveform(modulation_type_t mode)
{
    uint16_t samples[SONAR_LUT_MAX_SAMPLES];
    sonar_params_t params = sonar_adaptive_params(0.3f, 20.0f);
    uint32_t sample_count = sonar_fill_waveform(
        samples, SONAR_LUT_MAX_SAMPLES, &params, 1000000.0f,
        WINDOW_HAMMING, mode);
    uint16_t min_sample = DAC_FULL_SCALE;
    uint16_t max_sample = 0;

    assert(sample_count > 0 && sample_count <= SONAR_LUT_MAX_SAMPLES);

    for (uint32_t index = 0; index < sample_count; index++) {
        assert(samples[index] <= DAC_FULL_SCALE);
        if (samples[index] < min_sample) min_sample = samples[index];
        if (samples[index] > max_sample) max_sample = samples[index];
    }

    assert(min_sample < max_sample);
    for (uint32_t index = sample_count; index < SONAR_LUT_MAX_SAMPLES; index++) {
        assert(samples[index] == DAC_MID_SCALE);
    }
}

int main(void)
{
    test_adaptive_parameter_bounds();
    test_waveform(MOD_LFM);
    test_waveform(MOD_GEOMETRIC);
    test_waveform(MOD_PHASE_CODED);

    puts("waveform generator tests passed");
    return 0;
}