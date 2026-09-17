#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "human_target_angle/pose_mapping.h"

/*
 * Agent1 시각화용 Mock CNN stream generator
 *
 * 실제 CNN이 아직 없어도 10~20 Hz의 가변 주기, 좌표 jitter,
 * finger dropout, major landmark dropout, 순간 outlier를 포함한
 * HumanPose2D를 생성해 실제 pose_mapping_update()에 넣는다.
 *
 * 실행 결과는 CSV로 저장하며 Python 3D viewer에서 바로 읽을 수 있다.
 * Agent1 core 코드는 수정하지 않는다.
 */

#ifndef MOCK_PI_F
#define MOCK_PI_F 3.14159265358979323846f
#endif

static uint32_t g_rng = 0x13572468U;

static float rand01(void)
{
    g_rng = g_rng * 1664525U + 1013904223U;
    return (float)((g_rng >> 8) & 0x00FFFFFFU) / 16777215.0f;
}

static float rand_symmetric(float amplitude)
{
    return (2.0f * rand01() - 1.0f) * amplitude;
}

static Point2D p2(float x, float y)
{
    Point2D p;
    p.x = x;
    p.y = y;
    p.valid = 1U;
    return p;
}

static Point2D noisy(Point2D p, float amplitude_px)
{
    p.x += rand_symmetric(amplitude_px);
    p.y += rand_symmetric(amplitude_px);
    return p;
}

/*
 * 화면상 사람 우측 팔을 천천히 움직이는 synthetic trajectory.
 * shoulder span도 조금씩 바꿔 카메라와의 상대 depth 변화가 생기도록 한다.
 */
static void make_mock_pose(HumanPose2D *pose, uint32_t frame_id, float t_sec)
{
    float center_x;
    float center_y;
    float shoulder_span;
    float shoulder_rx;
    float shoulder_ry;
    float hand_angle;
    float hand_center_x;
    float hand_center_y;
    float finger_span;
    float span_angle;
    float sx;
    float sy;

    memset(pose, 0, sizeof(*pose));

    center_x = 320.0f + 10.0f * sinf(0.32f * t_sec);
    center_y = 190.0f +  5.0f * sinf(0.41f * t_sec);

    /* 120~150 px 정도로 변화: Z reconstruction 변화를 눈으로 확인하기 위함 */
    shoulder_span = 135.0f + 14.0f * sinf(0.27f * t_sec);

    pose->shoulder_l = p2(center_x - 0.5f * shoulder_span, center_y);
    pose->shoulder_r = p2(center_x + 0.5f * shoulder_span, center_y);

    shoulder_rx = pose->shoulder_r.x;
    shoulder_ry = pose->shoulder_r.y;

    /* 우측 팔: 어깨 -> 팔꿈치 -> 손목 */
    pose->elbow = p2(
        shoulder_rx + 52.0f + 17.0f * sinf(0.58f * t_sec),
        shoulder_ry + 34.0f + 16.0f * sinf(0.73f * t_sec)
    );

    pose->wrist = p2(
        pose->elbow.x + 54.0f + 12.0f * sinf(0.81f * t_sec),
        pose->elbow.y + 28.0f + 14.0f * cosf(0.66f * t_sec)
    );

    /* 손 방향과 finger span을 바꿔 wrist pitch/roll/gripper도 함께 움직이게 한다. */
    hand_angle = -0.35f + 0.30f * sinf(0.90f * t_sec);
    hand_center_x = pose->wrist.x + 30.0f * cosf(hand_angle);
    hand_center_y = pose->wrist.y + 30.0f * sinf(hand_angle);

    /* 약 8~24 px: close/open hysteresis를 모두 통과하도록 구성 */
    finger_span = 16.0f + 8.0f * sinf(0.52f * t_sec);
    span_angle = hand_angle + 0.5f * MOCK_PI_F + 0.35f * sinf(0.47f * t_sec);
    sx = 0.5f * finger_span * cosf(span_angle);
    sy = 0.5f * finger_span * sinf(span_angle);

    pose->finger1 = p2(hand_center_x - sx, hand_center_y - sy);
    pose->finger2 = p2(hand_center_x + sx, hand_center_y + sy);

    /* CNN이 실제로 줄 법한 작은 pixel jitter */
    pose->shoulder_l = noisy(pose->shoulder_l, 0.8f);
    pose->shoulder_r = noisy(pose->shoulder_r, 0.8f);
    pose->elbow      = noisy(pose->elbow,      1.2f);
    pose->wrist      = noisy(pose->wrist,      1.5f);
    pose->finger1    = noisy(pose->finger1,    1.8f);
    pose->finger2    = noisy(pose->finger2,    1.8f);

    /* ------------------------------------------------------------
     * 의도적인 CNN 이상 상황
     * ------------------------------------------------------------ */

    /* 4초 부근: finger2가 약 0.2초 누락 -> 손목/그리퍼만 HOLD 기대 */
    if (t_sec >= 4.0f && t_sec < 4.20f) {
        pose->finger2.valid = 0U;
    }

    /* 7초 부근: elbow 1 frame 정도 누락 -> 전체 Target HOLD 기대 */
    if (t_sec >= 7.00f && t_sec < 7.07f) {
        pose->elbow.valid = 0U;
    }

    /* 9초 부근: wrist 순간 큰 outlier -> tracking rejection 기대 */
    if (t_sec >= 9.00f && t_sec < 9.07f) {
        pose->wrist.x += 170.0f;
        pose->wrist.y -= 120.0f;
    }

    /* 12초 부근: finger 둘 다 약 0.3초 누락 -> hand HOLD 관찰 */
    if (t_sec >= 12.0f && t_sec < 12.30f) {
        pose->finger1.valid = 0U;
        pose->finger2.valid = 0U;
    }

    pose->frame_id = frame_id;
    pose->valid = 1U;
}

static float next_dt_sec(uint32_t frame_id)
{
    /* 10~20 Hz 사이를 오가는 deterministic pattern */
    static const float dt_table[] = {
        0.050f, 0.055f, 0.067f, 0.080f,
        0.100f, 0.071f, 0.060f, 0.091f
    };

    return dt_table[frame_id % (sizeof(dt_table) / sizeof(dt_table[0]))];
}

static void write_header(FILE *fp)
{
    fprintf(fp,
        "frame_id,time_sec,dt_sec,ret,target_valid,"
        "sl_x,sl_y,sl_valid,sr_x,sr_y,sr_valid,"
        "el_x,el_y,el_valid,wr_x,wr_y,wr_valid,"
        "f1_x,f1_y,f1_valid,f2_x,f2_y,f2_valid,"
        "fsl_x,fsl_y,fsr_x,fsr_y,fel_x,fel_y,fwr_x,fwr_y,ff1_x,ff1_y,ff2_x,ff2_y,"
        "sl3_x,sl3_y,sl3_z,sr3_x,sr3_y,sr3_z,"
        "el3_x,el3_y,el3_z,wr3_x,wr3_y,wr3_z,"
        "f13_x,f13_y,f13_z,f23_x,f23_y,f23_z,"
        "major3d_valid,finger3d_valid,"
        "base_deg,shoulder_deg,elbow_deg,wrist_pitch_deg,wrist_roll_deg,gripper_norm\n"
    );
}

static void write_row(
    FILE *fp,
    uint32_t frame_id,
    float time_sec,
    float dt_sec,
    int ret,
    const HumanPose2D *pose,
    const PoseMappingContext *ctx,
    const HumanJointTarget *target
)
{
    fprintf(fp,
        "%u,%.6f,%.6f,%d,%u,"
        "%.3f,%.3f,%u,%.3f,%.3f,%u,"
        "%.3f,%.3f,%u,%.3f,%.3f,%u,"
        "%.3f,%.3f,%u,%.3f,%.3f,%u,"
        "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
        "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
        "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
        "%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
        "%u,%u,"
        "%.3f,%.3f,%.3f,%.3f,%.3f,%.1f\n",
        frame_id, time_sec, dt_sec, ret, (unsigned)target->valid,
        pose->shoulder_l.x, pose->shoulder_l.y, (unsigned)pose->shoulder_l.valid,
        pose->shoulder_r.x, pose->shoulder_r.y, (unsigned)pose->shoulder_r.valid,
        pose->elbow.x, pose->elbow.y, (unsigned)pose->elbow.valid,
        pose->wrist.x, pose->wrist.y, (unsigned)pose->wrist.valid,
        pose->finger1.x, pose->finger1.y, (unsigned)pose->finger1.valid,
        pose->finger2.x, pose->finger2.y, (unsigned)pose->finger2.valid,
        ctx->shoulder_l.value.x, ctx->shoulder_l.value.y,
        ctx->shoulder_r.value.x, ctx->shoulder_r.value.y,
        ctx->elbow.value.x, ctx->elbow.value.y,
        ctx->wrist.value.x, ctx->wrist.value.y,
        ctx->finger1.value.x, ctx->finger1.value.y,
        ctx->finger2.value.x, ctx->finger2.value.y,
        ctx->shoulder_l_3d.x, ctx->shoulder_l_3d.y, ctx->shoulder_l_3d.z,
        ctx->shoulder_r_3d.x, ctx->shoulder_r_3d.y, ctx->shoulder_r_3d.z,
        ctx->elbow_3d.x, ctx->elbow_3d.y, ctx->elbow_3d.z,
        ctx->wrist_3d.x, ctx->wrist_3d.y, ctx->wrist_3d.z,
        ctx->finger1_3d.x, ctx->finger1_3d.y, ctx->finger1_3d.z,
        ctx->finger2_3d.x, ctx->finger2_3d.y, ctx->finger2_3d.z,
        (unsigned)ctx->major_pose3d_valid,
        (unsigned)ctx->finger_pose3d_valid,
        target->base_deg,
        target->shoulder_deg,
        target->elbow_deg,
        target->wrist_pitch_deg,
        target->wrist_roll_deg,
        target->gripper_norm
    );
}

int main(int argc, char **argv)
{
    const char *csv_path = (argc >= 2) ? argv[1] : "agent1_visual.csv";
    FILE *fp;
    PoseMappingContext ctx;
    HumanPose2D pose;
    HumanJointTarget target;
    uint32_t frame_id;
    float t_sec = 0.0f;

    fp = fopen(csv_path, "w");
    if (fp == NULL) {
        perror("fopen");
        return 1;
    }

    if (pose_mapping_init(&ctx) != 0) {
        fprintf(stderr, "pose_mapping_init failed\n");
        fclose(fp);
        return 1;
    }

    memset(&target, 0, sizeof(target));
    write_header(fp);

    for (frame_id = 1U; t_sec < 16.0f; ++frame_id) {
        float dt_sec = next_dt_sec(frame_id);
        int ret;

        t_sec += dt_sec;
        make_mock_pose(&pose, frame_id, t_sec);

        ret = pose_mapping_update(
            &ctx,
            &pose,
            POSE_ARM_RIGHT,
            dt_sec,
            &target
        );

        write_row(fp, frame_id, t_sec, dt_sec, ret, &pose, &ctx, &target);

        if ((frame_id % 20U) == 0U) {
            printf(
                "t=%5.2fs frame=%3u valid=%u  base=%7.2f sh=%7.2f el=%7.2f "
                "pitch=%7.2f roll=%7.2f grip=%.0f  wrist_z=%6.3f\n",
                t_sec,
                frame_id,
                (unsigned)target.valid,
                target.base_deg,
                target.shoulder_deg,
                target.elbow_deg,
                target.wrist_pitch_deg,
                target.wrist_roll_deg,
                target.gripper_norm,
                ctx.wrist_3d.z
            );
        }
    }

    fclose(fp);
    printf("\nMock CNN visual stream complete.\nCSV: %s\n", csv_path);
    printf("Python: python3 tools/visualize_agent1_3d.py %s\n", csv_path);
    printf("Z plot:  python3 tools/plot_agent1_z.py %s\n", csv_path);

    return 0;
}
