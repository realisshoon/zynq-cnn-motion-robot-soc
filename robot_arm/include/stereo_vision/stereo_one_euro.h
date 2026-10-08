#ifndef STEREO_VISION_ONE_EURO_H
#define STEREO_VISION_ONE_EURO_H

#include <stdint.h>

#define STEREO_ONE_EURO_RESET_GAP_US UINT64_C(500000)

typedef struct {
    float minimum_cutoff_hz;
    float beta_per_mm;
    float derivative_cutoff_hz;
} StereoOneEuroConfig;

typedef struct {
    float previous_raw[3];
    float filtered[3];
    float derivative[3];
    uint64_t last_time_us;
    unsigned have_time;
    StereoOneEuroConfig config;
} StereoOneEuro;

StereoOneEuroConfig stereo_one_euro_defaults(void);
void stereo_one_euro_init(StereoOneEuro *state, StereoOneEuroConfig config);
int stereo_one_euro_step(StereoOneEuro *state, const float input[3], int valid,
                         uint64_t time_us, float output[3]);

#endif
