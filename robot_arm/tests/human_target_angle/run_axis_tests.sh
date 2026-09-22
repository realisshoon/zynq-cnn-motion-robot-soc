#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
result_dir=$(mktemp -d /tmp/agent1-axis-tests.XXXXXX)
printf 'Results: %s\n' "$result_dir"

cc=${CC:-gcc}
flags=(-std=c99 -Wall -Wextra -Wpedantic -Iinclude -Iconfig)
a1=(src/human_target_angle/pose_{mapping,math,tracking,reconstruction,joint,hand}.c)
a1+=(src/human_target_angle/forearm_mapping.c)

for name in body_frame pose_mapping pose_csv pose_visual; do
    "$cc" "${flags[@]}" "${a1[@]}" \
        "tests/human_target_angle/test_${name}.c" -lm \
        -o "$result_dir/test_$name"
done

"$result_dir/test_body_frame"
"$result_dir/test_pose_mapping"
"$result_dir/test_pose_csv" \
    etc/example_pose2d_1280x720_20hz.csv "$result_dir/pose_csv.csv" --demo-table
awk -F, 'NR>1 {n++; if ($3==1 && $4==1) ok++; if (tolower($0) ~ /nan|inf/) bad++}
    END {if (n!=522 || ok!=522 || bad) exit 1}' "$result_dir/pose_csv.csv"
"$result_dir/test_pose_visual" "$result_dir/pose_visual.csv"

"$cc" "${flags[@]}" "${a1[@]}" src/uart_pose/uart_pose_protocol.c \
    tests/uart_pose/test_uart_agent1_pc.c -lm -o "$result_dir/test_uart"
"$result_dir/test_uart" \
    etc/uart_pose_stream.bin "$result_dir/uart.csv" right 0.05
awk -F, 'NR>1 {n++; if ($10==1 && $11==1) ok++; if (tolower($0) ~ /nan|inf/) bad++}
    END {if (n!=522 || ok!=522 || bad) exit 1}' "$result_dir/uart.csv"

printf 'Agent1 axis suite: PASS\n'
