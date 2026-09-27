#include "output_controller/servo_control.h"
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
int main(void)
{
    unsigned i;
    uint16_t out;
    for (i = 0; i < SERVO_COUNT; ++i) {
        ServoChannel ch = (ServoChannel)i;
        const float upper = ch == SERVO_GRIPPER ? 1.0f : 180.0f;
        const float inputs[] = {-10.0f, 0.0f, upper/4, upper/2, upper*3/4, upper, upper+10};
        /* Gripper is inverted at the PWM layer: 0.0=Close maps to max_us,
         * 1.0=Open maps to min_us (verified servo direction), while the
         * A1 gripper_norm contract (0=Close/1=Open) stays unchanged. */
        const uint16_t increasing[] = {500, 500, 1000, 1500, 2000, 2500, 2500};
        const uint16_t decreasing[] = {2500, 2500, 2000, 1500, 1000, 500, 500};
        const uint16_t *expected = ch == SERVO_GRIPPER ? decreasing : increasing;
        unsigned j;
        for (j = 0; j < 7; ++j) {
            assert(servo_control_convert_channel(ch, inputs[j], &out));
            assert(out == expected[j]);
        }
        const float invalid[] = {NAN, INFINITY, -INFINITY};
        for (j = 0; j < 3; ++j) {
            out = 1234;
            assert(!servo_control_convert_channel(ch, invalid[j], &out));
            assert(out == 1234);
        }
        assert(!servo_control_convert_channel(ch, 0, NULL));
    }
    out = 1234;
    assert(!servo_control_convert_channel((ServoChannel)5, 0, &out));
    assert(!servo_control_convert_channel((ServoChannel)-1, 0, &out));
    assert(out == 1234);
    puts("PASS PWM conversion: five channels, clamp, interpolation, non-finite rejection");
    return 0;
}
