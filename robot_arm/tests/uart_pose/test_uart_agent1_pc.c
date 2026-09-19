/*
 * XSA 없이 PC에서 하는 UART + Agent1 통합 테스트
 *
 * 실제 흐름:
 *
 * uart_pose_stream.bin
 *       |
 *       | fread() 1 byte
 *       v
 * pose_uart_parser_push()
 *       |
 *       | packet complete
 *       v
 * HumanPose2D
 *       |
 *       v
 * pose_mapping_update()
 *       |
 *       v
 * HumanJointTarget
 *
 * 실제 UART 대신 binary file을 byte stream으로 사용한다.
 * 따라서 XUartPs/XSA/보드 UART만 제외하고 UART packet부터 Agent1까지
 * 동일한 구조를 테스트한다.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "uart_pose/uart_pose_protocol.h"
#include "human_target_angle/pose_mapping.h"

#define DEFAULT_DT_SEC 0.050f

static PoseArmSide parse_arm(const char *s)
{
    if (s != NULL && strcmp(s, "left") == 0) {
        return POSE_ARM_LEFT;
    }
    return POSE_ARM_RIGHT;
}

static void write_csv_header(FILE *fp)
{
    fprintf(
        fp,
        "frame_id,parser_ret,pose_valid,"
        "f1_valid,f2_valid,elbow_valid,wrist_valid,sl_valid,sr_valid,"
        "agent1_ret,target_valid,"
        "base_deg,shoulder_deg,elbow_deg,wrist_pitch_deg,wrist_roll_deg\n"
    );
}

static void write_result_row(
    FILE *fp,
    const HumanPose2D *pose,
    int parser_ret,
    int agent1_ret,
    const HumanJointTarget *target
)
{
    fprintf(
        fp,
        "%lu,%d,%u,"
        "%u,%u,%u,%u,%u,%u,"
        "%d,%u,"
        "%.6f,%.6f,%.6f,%.6f,%.6f\n",
        (unsigned long)pose->frame_id,
        parser_ret,
        (unsigned int)pose->valid,
        (unsigned int)pose->finger1.valid,
        (unsigned int)pose->finger2.valid,
        (unsigned int)pose->elbow.valid,
        (unsigned int)pose->wrist.valid,
        (unsigned int)pose->shoulder_l.valid,
        (unsigned int)pose->shoulder_r.valid,
        agent1_ret,
        (unsigned int)target->valid,
        target->base_deg,
        target->shoulder_deg,
        target->elbow_deg,
        target->wrist_pitch_deg,
        target->wrist_roll_deg
    );
}

int main(int argc, char **argv)
{
    const char *stream_path;
    const char *output_path;
    PoseArmSide active_arm;
    float dt_sec;

    FILE *stream_fp;
    FILE *output_fp;

    PoseUartParser parser;
    PoseMappingContext agent1_ctx;

    HumanPose2D pose;
    HumanJointTarget target;

    uint8_t byte;
    uint32_t total_bytes = 0U;
    uint32_t decoded_frames = 0U;
    uint32_t agent1_updates = 0U;
    uint32_t agent1_holds = 0U;
    uint32_t agent1_invalids = 0U;

    if (argc < 3) {
        fprintf(
            stderr,
            "usage: %s <uart_stream.bin> <result.csv> [right|left] [dt_sec]\n",
            argv[0]
        );
        return 1;
    }

    stream_path = argv[1];
    output_path = argv[2];
    active_arm = parse_arm((argc >= 4) ? argv[3] : "right");
    dt_sec = (argc >= 5) ? (float)atof(argv[4]) : DEFAULT_DT_SEC;

    if (!(dt_sec > 0.0f)) {
        fprintf(stderr, "[ERR] dt_sec must be > 0\n");
        return 1;
    }

    stream_fp = fopen(stream_path, "rb");
    if (stream_fp == NULL) {
        perror("[ERR] fopen input");
        return 1;
    }

    output_fp = fopen(output_path, "w");
    if (output_fp == NULL) {
        perror("[ERR] fopen output");
        fclose(stream_fp);
        return 1;
    }

    pose_uart_parser_init(&parser);

    if (pose_mapping_init(&agent1_ctx) != 0) {
        fprintf(stderr, "[ERR] pose_mapping_init failed\n");
        fclose(stream_fp);
        fclose(output_fp);
        return 1;
    }

    memset(&pose, 0, sizeof(pose));
    memset(&target, 0, sizeof(target));

    write_csv_header(output_fp);

    while (fread(&byte, 1U, 1U, stream_fp) == 1U) {
        int parser_ret;

        total_bytes++;

        parser_ret = pose_uart_parser_push(
            &parser,
            byte,
            &pose
        );

        if (parser_ret == 1) {
            int agent1_ret;

            decoded_frames++;

            agent1_ret = pose_mapping_update(
                &agent1_ctx,
                &pose,
                active_arm,
                dt_sec,
                &target
            );

            if (agent1_ret == 1) {
                agent1_updates++;
            } else if (agent1_ret == 0) {
                agent1_holds++;
            } else {
                agent1_invalids++;
            }

            write_result_row(
                output_fp,
                &pose,
                parser_ret,
                agent1_ret,
                &target
            );

            if ((decoded_frames % 20U) == 0U) {
                printf(
                    "[frame %lu] A1 ret=%d valid=%u "
                    "base=%7.2f sh=%7.2f el=%7.2f "
                    "pitch=%7.2f roll=%7.2f\n",
                    (unsigned long)pose.frame_id,
                    agent1_ret,
                    (unsigned int)target.valid,
                    target.base_deg,
                    target.shoulder_deg,
                    target.elbow_deg,
                    target.wrist_pitch_deg,
                    target.wrist_roll_deg
                );
            }
        }
    }

    fclose(stream_fp);
    fclose(output_fp);

    printf("\n=== UART + Agent1 PC test ===\n");
    printf("bytes read      : %lu\n", (unsigned long)total_bytes);
    printf("packets OK      : %lu\n", (unsigned long)parser.packets_ok);
    printf("CRC errors      : %lu\n", (unsigned long)parser.crc_errors);
    printf("format errors   : %lu\n", (unsigned long)parser.format_errors);
    printf("range errors    : %lu\n", (unsigned long)parser.range_errors);
    printf("decoded frames  : %lu\n", (unsigned long)decoded_frames);
    printf("Agent1 update   : %lu\n", (unsigned long)agent1_updates);
    printf("Agent1 hold     : %lu\n", (unsigned long)agent1_holds);
    printf("Agent1 invalid  : %lu\n", (unsigned long)agent1_invalids);
    printf("result          : %s\n", output_path);

    if (parser.crc_errors != 0U ||
        parser.format_errors != 0U) {
        return 2;
    }

    return 0;
}
