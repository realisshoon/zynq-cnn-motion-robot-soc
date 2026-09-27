#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "human_target_angle/forearm_mapping.h"
#include "robot_calibration/forearm_calibration.h"
#include "robot_calibration/forearm_safety_check.h"

/*
 * etc/example_pose2d_1280x720_20hz.csv를 실제 Agent1 forearm_mapping_update()에
 * 넣어 재생하고, 그 결과를 이번에 새로 만든 Agent2 5축 모듈(unwrap -> map ->
 * limits -> safety_check -> ramp)에 그대로 흘려서 승인/거부 통계를 낸다.
 *
 * 2026-09-22: Agent1이 TableFrame(외부 up/forward 보정) 요구를 없애고 기존
 * 6축과 같은 BodyFrame(어깨 기반)으로 바꿔서, 이제 별도 설정 없이
 * forearm_mapping_init() 하나만 부르면 된다.
 *
 * A1은 CSV timestamp 간격, A2는 별도의 고정 20ms 틱으로 실행한다.
 * 목표가 거부돼도 마지막 승인 목표의 틱은 계속 돈다. 하드웨어 호출은 없다.
 * argv[2]를 주면 raw/unwrap/목표/FK/판정을 CSV로 저장한다.
 */

static Point2D make_p2(float x, float y, int valid)
{
    Point2D p;
    p.x = x; p.y = y; p.valid = valid ? 1U : 0U;
    return p;
}

static void joints(const ForearmJointCommand *c, float a[4])
{
    a[0]=c->elbow_roll_deg; a[1]=c->elbow_pitch_deg;
    a[2]=c->wrist_pitch_deg; a[3]=c->wrist_roll_deg;
}

static void tick(ForearmMotionState *state, ForearmJointCommand *previous,
                 float *max_step, int *blocked_ticks)
{
    ForearmJointCommand output;
    float before[4],after[4];
    joints(previous,before);
    forearm_calibration_step(state,&output);
    joints(&output,after);
    assert(output.valid && forearm_safety_check_apply(&output,NULL));
    for (int i=0; i<4; i++) {
        float delta=fabsf(after[i]-before[i]);
        assert(isfinite(after[i]) && after[i]>=20 && after[i]<=160);
        assert(delta<=0.6001f);
        if (delta>*max_step) *max_step=delta;
    }
    if (state->blocked_flags) (*blocked_ticks)++;
    *previous=output;
}

int main(int argc, char **argv)
{
    const char *in_name = (argc >= 2) ? argv[1] : "etc/example_pose2d_1280x720_20hz.csv";
    FILE *in = fopen(in_name, "r");
    FILE *csv = argc>=3 ? fopen(argv[2],"w") : NULL;
    char line[2048];
    ForearmMappingContext mapping;
    HumanPose2D pose;
    HumanForearmTarget a1_target;
    ForearmAngleUnwrapState unwrap;
    ForearmMotionState state;
    ForearmJointCommand cmd, prev_cmd={90,90,90,90,0.5f,1};
    float prev_t = 0.0f;
    int first = 1;
    int rows = 0, fresh = 0, hold = 0, invalid = 0, roll_unobservable = 0, hand_hold = 0;
    int a2_accepted = 0, a2_rejected = 0;
    float yaw_min = 1e9f, yaw_max = -1e9f, pitch_min = 1e9f, pitch_max = -1e9f;
    float roll_cmd_min = 1e9f, roll_cmd_max = -1e9f;
    float max_step = 0.0f;
    double next_tick=0.02;
    int ticks=0, blocked_ticks=0, saturation[4]={0,0,0,0};
    int wrist_below=0, tip_only=0, self_collision=0;
    uint32_t reject_flags_seen = 0;

    if (!in) { fprintf(stderr, "input open failed: %s\n", in_name); return 1; }
    if (argc>=3) assert(csv);
    if (csv) fputs("fid,time,raw_yaw,raw_pitch,raw_wp,raw_wr,unwrap_yaw,unwrap_wp,unwrap_wr,cmd_yaw,cmd_pitch,cmd_wp,cmd_wr,wrist_z,tip_z,accepted,flags\n",csv);

    if (forearm_mapping_init(&mapping) != 0) { fprintf(stderr, "forearm_mapping_init failed\n"); return 1; }
    forearm_motion_control_unwrap_state_init(&unwrap);
    forearm_calibration_state_init(&state);
    forearm_calibration_set_target(&state,&prev_cmd); /* Simulated known neutral start. */

    if (!fgets(line, sizeof(line), in)) { fclose(in); return 1; } /* header skip */

    while (fgets(line, sizeof(line), in)) {
        uint32_t frame_id;
        float time_sec, slx, sly, srx, sry, ex, ey, wx, wy, f1x, f1y, f2x, f2y;
        int frame_valid, slv, srv, ev, wv, f1v, f2v;
        int n = sscanf(line,
            "%u,%f,%d,%f,%f,%d,%f,%f,%d,%f,%f,%d,%f,%f,%d,%f,%f,%d,%f,%f,%d",
            &frame_id, &time_sec, &frame_valid,
            &slx, &sly, &slv, &srx, &sry, &srv, &ex, &ey, &ev,
            &wx, &wy, &wv, &f1x, &f1y, &f1v, &f2x, &f2y, &f2v);
        assert(n == 21);
        while (next_tick <= (double)time_sec) {
            tick(&state,&prev_cmd,&max_step,&blocked_ticks);
            ticks++; next_tick+=0.02;
        }

        memset(&pose, 0, sizeof(pose));
        pose.shoulder_l = make_p2(slx, sly, slv);
        pose.shoulder_r = make_p2(srx, sry, srv);
        pose.elbow = make_p2(ex, ey, ev);
        pose.wrist = make_p2(wx, wy, wv);
        pose.finger1 = make_p2(f1x, f1y, f1v);
        pose.finger2 = make_p2(f2x, f2y, f2v);
        pose.frame_id = frame_id;
        pose.valid = frame_valid ? 1U : 0U;

        {
            float dt = first ? (1.0f / 20.0f) : (time_sec - prev_t);
            int ret;
            if (dt <= 0.0f) dt = 1.0f / 20.0f;
            prev_t = time_sec; first = 0;

            ret = forearm_mapping_update(&mapping, &pose, POSE_ARM_RIGHT, dt, &a1_target);
            rows++;
            if (ret == 1) fresh++;
            else if (ret == 0) hold++;
            else invalid++;
            if (a1_target.elbow_roll_observable == 0) roll_unobservable++;
            if (a1_target.hand_fresh == 0) hand_hold++;
        }

        if (!a1_target.valid) continue; /* invalid: A2로 넘길 target이 없다 */

        if (a1_target.elbow_roll_deg < yaw_min) yaw_min = a1_target.elbow_roll_deg;
        if (a1_target.elbow_roll_deg > yaw_max) yaw_max = a1_target.elbow_roll_deg;
        if (a1_target.elbow_pitch_deg < pitch_min) pitch_min = a1_target.elbow_pitch_deg;
        if (a1_target.elbow_pitch_deg > pitch_max) pitch_max = a1_target.elbow_pitch_deg;

        {
            HumanForearmTarget raw=a1_target;
            ForearmSafetyCheckFlags flags = 0;
            ForearmJointCommand mapped_cmd;
            ForearmJointPositions3D positions;
            float values[4];
            int accepted;
            assert(forearm_motion_control_validate_target(&a1_target));
            forearm_motion_control_unwrap_target(&unwrap,&a1_target);
            accepted=forearm_calibration_apply(&a1_target,&cmd);
            forearm_motion_control_map_target(&a1_target, &mapped_cmd);
            forearm_motion_control_apply_limits(&mapped_cmd);
            mapped_cmd.valid = 1;
            assert(forearm_safety_check_apply(&mapped_cmd,&flags)==accepted);
            assert(forearm_robot_forward_kinematics_3d(&mapped_cmd,&positions));
            joints(&mapped_cmd,values);
            for (int i=0; i<4; i++) if (values[i]==20 || values[i]==160) saturation[i]++;
            if (accepted) {
                a2_accepted++;
                forearm_calibration_set_target(&state,&cmd);
                if (cmd.elbow_roll_deg<roll_cmd_min) roll_cmd_min=cmd.elbow_roll_deg;
                if (cmd.elbow_roll_deg>roll_cmd_max) roll_cmd_max=cmd.elbow_roll_deg;
            } else {
                a2_rejected++;
                /* 2026-09-22: wrist_pitch=90=90도 굽힘으로 바뀌면서 자기충돌
                 * (wrist_pitch>155)도 [20,160] 범위 안에서 도달 가능해졌다
                 * -- 더 이상 테이블충돌만 거부 사유가 아니다. */
                assert(!cmd.valid && (flags==FOREARM_SAFETY_CHECK_TABLE_COLLISION ||
                       flags==FOREARM_SAFETY_CHECK_SELF_COLLISION ||
                       flags==(FOREARM_SAFETY_CHECK_TABLE_COLLISION|FOREARM_SAFETY_CHECK_SELF_COLLISION)));
                reject_flags_seen|=flags;
                if (flags & FOREARM_SAFETY_CHECK_SELF_COLLISION) self_collision++;
                if (positions.wrist.z_cm<=-5) wrist_below++;
                else if (positions.tip.z_cm<=-5) tip_only++;
            }
            if (csv) fprintf(csv,"%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%u\n",
                (unsigned)frame_id,time_sec,raw.elbow_roll_deg,raw.elbow_pitch_deg,
                raw.wrist_pitch_deg,raw.wrist_roll_deg,a1_target.elbow_roll_deg,
                a1_target.wrist_pitch_deg,a1_target.wrist_roll_deg,
                values[0],values[1],values[2],values[3],positions.wrist.z_cm,
                positions.tip.z_cm,accepted,(unsigned)flags);
        }
    }
    fclose(in);
    if (csv) fclose(csv);
    /* Keep ticking after input ends; a blocked path may hold instead of arriving. */
    for (int i=0; i<400; i++) { tick(&state,&prev_cmd,&max_step,&blocked_ticks); ticks++; }

    printf("rows=%d fresh=%d hold=%d invalid=%d roll_unobservable=%d hand_hold=%d\n",
        rows, fresh, hold, invalid, roll_unobservable, hand_hold);
    printf("A1 forearm_yaw range %.2f..%.2f, forearm_pitch range %.2f..%.2f\n",
        yaw_min, yaw_max, pitch_min, pitch_max);
    printf("A2 accepted=%d rejected=%d (reject reason flags seen=0x%x)\n",
        a2_accepted, a2_rejected, reject_flags_seen);
    printf("A2 accepted elbow_roll target range %.2f..%.2f; limit frames yaw/pitch/wp/wr=%d/%d/%d/%d\n",
        roll_cmd_min,roll_cmd_max,saturation[0],saturation[1],saturation[2],saturation[3]);
    printf("A2 rejected wrist<=table=%d tip-only=%d self_collision=%d; ticks=%d blocked=%d max_step=%.6f deg/20ms\n",
        wrist_below,tip_only,self_collision,ticks,blocked_ticks,max_step);

    /* 2026-09-22: Agent1이 TableFrame->BodyFrame으로 바꾸면서 수치가 소폭
     * 달라졌다(카메라 데모 축과 사람 어깨 기반 축이 이 클립에서는 거의
     * 비슷한 방향이라 크게 다르진 않음). 직접 실행해서 재확인한 값이다.
     * 2026-09-23: dev/robot PR#57(Agent1 손목 기하 수정, 부모 이동/depth 지연
     * 보정 + pitch 35도/frame 제한)이 병합되면서 hand_hold/A2 승인 수가 다시
     * 바뀌었다 -- yaw/pitch 범위는 0.01도 이내로 그대로였고, 실행해서
     * 재확인한 값으로 갱신했다. */
    assert(rows == 522);
    assert(fresh == 522);
    assert(invalid == 0);
    assert(hold == 0);
    assert(roll_unobservable == 1);
    assert(hand_hold == 2);
    assert(fabsf(yaw_min-(-176.24f))<0.01f && fabsf(yaw_max-179.11f)<0.01f);
    assert(fabsf(pitch_min-(-84.59f))<0.01f && fabsf(pitch_max-16.69f)<0.01f);
    assert(a2_accepted==484 && a2_rejected==38);
    assert(reject_flags_seen==FOREARM_SAFETY_CHECK_SELF_COLLISION);
    assert(self_collision==38 && wrist_below==0 && tip_only==0);

    puts("test_forearm_replay: PASS (A1 wiring cross-check + A2 accept/reject over real demo motion)");
    return 0;
}
