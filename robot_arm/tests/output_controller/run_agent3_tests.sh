#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
build_dir=$(mktemp -d /tmp/agent3-tests.XXXXXX)
trap 'rm -rf "$build_dir"' EXIT
for name in servo_hal servo_control output_control; do
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -Iinclude \
        "tests/output_controller/test_${name}.c" \
        src/output_controller/servo_config.c src/output_controller/servo_control.c \
        src/output_controller/output_control.c src/output_controller/servo_hal.c \
        src/drivers/servo_pwm_driver.c -lm -o "$build_dir/test_${name}"
    "$build_dir/test_${name}"
done
# Verify mock-only symbols are excluded without requiring the Xilinx BSP.
# This checks test translation units only, not a real-driver board build.
for name in servo_hal output_control; do
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -Iinclude \
        -DSERVO_PWM_DRIVER_USE_XILINX -fsyntax-only \
        "tests/output_controller/test_${name}.c"
done
echo "PASS Xilinx test boundary: test translation units compile without mock APIs"
