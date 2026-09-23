#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "robot_calibration/forearm_calibration.h"

static void near(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.002f);
}

static HumanForearmTarget target(float yaw, float pitch, float wp, float wr, float grip)
{
    HumanForearmTarget t;
    t.elbow_roll_deg = yaw;
    t.elbow_pitch_deg = pitch;
    t.wrist_pitch_deg = wp;
    t.wrist_roll_deg = wr;
    t.gripper_norm = grip;
    t.frame_id = 1;
    t.valid = 1;
    t.elbow_roll_observable = 1;
    t.hand_fresh = 1;
    return t;
}

static ForearmJointCommand apply(HumanForearmTarget t)
{
    ForearmJointCommand c;
    assert(forearm_calibration_apply(&t, &c));
    assert(c.valid);
    return c;
}

static void test_validate(void)
{
    HumanForearmTarget t = target(0, 0, 0, 0, 0.5f);
    assert(forearm_motion_control_validate_target(&t));
    assert(!forearm_motion_control_validate_target(NULL));
    t.valid = 0; assert(!forearm_motion_control_validate_target(&t));
    t.valid = 1;
    t.elbow_roll_deg = NAN; assert(!forearm_motion_control_validate_target(&t));
    t = target(0, 91, 0, 0, 0.5f); assert(!forearm_motion_control_validate_target(&t));
    t = target(0, -91, 0, 0, 0.5f); assert(!forearm_motion_control_validate_target(&t));
    t = target(0, 90, 0, 0, 0.5f); assert(forearm_motion_control_validate_target(&t)); /* 경계값 */
    t = target(0, 0, 0, 0, 1.5f); assert(!forearm_motion_control_validate_target(&t));
    t = target(0, 0, 0, 0, -0.1f); assert(!forearm_motion_control_validate_target(&t));
    /* yaw/wrist_pitch/wrist_roll는 주기적이라 범위를 보지 않는다. */
    t = target(720, 0, -500, 999, 0.5f); assert(forearm_motion_control_validate_target(&t));
    for (int i=0; i<5; i++) {
        float *fields[5];
        t=target(0,0,0,0,0.5f);
        fields[0]=&t.elbow_roll_deg; fields[1]=&t.elbow_pitch_deg;
        fields[2]=&t.wrist_pitch_deg; fields[3]=&t.wrist_roll_deg;
        fields[4]=&t.gripper_norm;
        *fields[i]=INFINITY;
        assert(!forearm_motion_control_validate_target(&t));
        *fields[i]=NAN;
        assert(!forearm_motion_control_validate_target(&t));
    }
}

static ForearmJointCommand mapped(HumanForearmTarget t)
{
    /* forearm_calibration_apply()는 안전검사까지 통과해야 하므로, 순수
     * 매핑/clamp 산술만 볼 때는 그 두 함수만 직접 부른다(안전검사와
     * 매핑 산술은 별개로 검증한다 -- 안전검사는 test_forearm_safety_check.c). */
    ForearmJointCommand c;
    forearm_motion_control_map_target(&t, &c);
    forearm_motion_control_apply_limits(&c);
    return c;
}

static void test_map_and_limits(void)
{
    ForearmJointCommand c;

    /* scale=1,direction=1,zero_offset=90인 현재 임시 설정에서는 항등 매핑. */
    c = mapped(target(0, 0, 0, 0, 0.0f));
    near(c.elbow_roll_deg, 90); near(c.elbow_pitch_deg, 90);
    near(c.wrist_pitch_deg, 90); near(c.wrist_roll_deg, 90);
    near(c.gripper_norm, 0.0f);

    c = mapped(target(30, -20, 45, -45, 1.0f));
    near(c.elbow_roll_deg, 120); near(c.elbow_pitch_deg, 70);
    near(c.wrist_pitch_deg, 135); near(c.wrist_roll_deg, 45);
    near(c.gripper_norm, 1.0f);

    /* clamp: [20,160] 밖으로 나가는 큰 값. */
    {
        c = mapped(target(200, 0, 200, -200, 0.5f));
        near(c.elbow_roll_deg, 160);
        near(c.wrist_pitch_deg, 160);
        near(c.wrist_roll_deg, 20);
    }

    /* forearm_calibration_apply()는 매핑+clamp+안전검사를 모두 거친다 --
     * 물리적으로 안전한(테이블/자기충돌 없는) 자세로만 종단 검증한다. */
    c = apply(target(0, 0, 0, 0, 0.0f));
    near(c.elbow_roll_deg, 90); near(c.elbow_pitch_deg, 90);

    for (int sign=-1; sign<=1; sign+=2) {
        float edge=sign<0 ? 20.0f : 160.0f;
        c=mapped(target(sign*70.0f,sign*70.0f,sign*70.0f,sign*70.0f,0.5f));
        near(c.elbow_roll_deg,edge); near(c.elbow_pitch_deg,edge);
        near(c.wrist_pitch_deg,edge); near(c.wrist_roll_deg,edge);
        c=mapped(target(sign*70.1f,sign*70.1f,sign*70.1f,sign*70.1f,0.5f));
        near(c.elbow_roll_deg,edge); near(c.elbow_pitch_deg,edge);
        near(c.wrist_pitch_deg,edge); near(c.wrist_roll_deg,edge);
    }

    {
        ForearmJointCommand raw = {-100, 500, 300, -50, 2.0f, 1};
        forearm_motion_control_apply_limits(&raw);
        near(raw.elbow_roll_deg, 20); near(raw.elbow_pitch_deg, 160);
        near(raw.wrist_pitch_deg, 160); near(raw.wrist_roll_deg, 20);
        near(raw.gripper_norm, 2.0f); /* gripper는 여기서 clamp 대상이 아니다. */
    }

    assert(!forearm_calibration_apply(NULL, &c)); assert(!c.valid);
    assert(!forearm_calibration_apply(&(HumanForearmTarget){0}, NULL));
    forearm_motion_control_map_target(NULL, &c);
    forearm_motion_control_apply_limits(NULL);
}

static void test_unwrap(void)
{
    ForearmAngleUnwrapState state;
    HumanForearmTarget t = target(179, 0, 0, 0, 0.5f);

    forearm_motion_control_unwrap_state_init(&state);
    forearm_motion_control_unwrap_target(&state, &t);
    near(t.elbow_roll_deg, 179);

    t = target(-179, 0, 0, 0, 0.5f);
    forearm_motion_control_unwrap_target(&state, &t);
    near(t.elbow_roll_deg, 181); /* 최단 회전으로 풀면 -179가 아니라 181 */

    /* 직전까지 state.wrist_pitch_deg/wrist_roll_deg는 0(두 호출 모두 입력 0).
     * -1250 -> wrap_to_180 -> -170 (기준 0에서 최단회전). 602.5 -> 242.5 ->
     * wrap_to_180 -> -117.5. */
    t.elbow_roll_deg += 720;
    t.wrist_pitch_deg = -1250;
    t.wrist_roll_deg = 602.5f;
    forearm_motion_control_unwrap_target(&state, &t);
    near(t.wrist_pitch_deg, -170.0f);
    near(t.wrist_roll_deg, -117.5f);

    forearm_motion_control_unwrap_state_init(NULL);
    forearm_motion_control_unwrap_target(NULL, &t);
    forearm_motion_control_unwrap_target(&state, NULL);
}

static void test_ramp_speed_and_retarget(void)
{
    ForearmMotionState s;
    ForearmJointCommand rest = apply(target(0, 0, 0, 0, 0.0f));
    ForearmJointCommand goal = rest, c, prev;

    forearm_calibration_state_init(&s);
    forearm_calibration_set_target(&s, &rest);
    forearm_calibration_step(&s, &c);
    near(c.elbow_roll_deg, 90);

    goal.elbow_roll_deg = 150; goal.elbow_pitch_deg = 120; goal.wrist_pitch_deg = 110;
    goal.wrist_roll_deg = 130;
    goal.gripper_norm = 1;
    forearm_calibration_set_target(&s, &goal);
    prev = c;
    for (int tick = 0; tick < 200; tick++) {
        forearm_calibration_step(&s, &c);
        assert(fabsf(c.elbow_roll_deg - prev.elbow_roll_deg) <= 0.6001f);
        assert(fabsf(c.elbow_pitch_deg - prev.elbow_pitch_deg) <= 0.6001f);
        assert(fabsf(c.wrist_pitch_deg - prev.wrist_pitch_deg) <= 0.6001f);
        assert(fabsf(c.wrist_roll_deg - prev.wrist_roll_deg) <= 0.6001f);
        near(c.gripper_norm, 1);
        if (tick < 40) { assert(c.elbow_roll_deg < 150); assert(c.elbow_pitch_deg < 120); }
        prev = c;
    }
    near(c.elbow_roll_deg, 150); near(c.elbow_pitch_deg, 120); near(c.wrist_pitch_deg, 110);
    near(c.wrist_roll_deg,130);

    /* 이동 중 재목표(streaming retarget): 속도 제한은 계속 지키되 선형 추종. */
    forearm_calibration_set_target(&s, &rest);
    for (int tick = 0; tick < 5; tick++) {
        forearm_calibration_step(&s, &c);
        goal.elbow_roll_deg -= 1.0f;
        forearm_calibration_set_target(&s, &goal);
        prev = c;
        forearm_calibration_step(&s, &c);
        assert(fabsf(c.elbow_roll_deg - prev.elbow_roll_deg) <= 0.6001f);
    }

    forearm_calibration_state_init(NULL);
    forearm_calibration_set_target(NULL, &rest);
    forearm_calibration_set_target(&s, NULL);
    forearm_calibration_step(NULL, &c);
    forearm_calibration_step(&s, NULL);
}

static ForearmJointCommand raw_command(float roll, float pitch, float wp, float wr)
{
    ForearmJointCommand c = {roll, pitch, wp, wr, 0.5f, 1};
    return c;
}

/*
 * 2026-09-22 좌표계 수정(elbow_pitch=90=수직, wrist_pitch=90=전완 기준
 * 90도 굽힘) 이후: [20,160] 클램프 범위 안에서는 전역 최소 높이가
 * -1.79cm로, 어떤 조합도 테이블(-5cm)에는 닿지 않는다(스크립트로 전수
 * 탐색 확인. wrist_pitch가 90도 굽힘 기준으로 바뀌면서 여유가
 * +0.548cm에서 줄었지만 아직은 안전하다). 즉 지금 확정된 관절범위에서는
 * 테이블 충돌이 실제로 도달 불가능한 상태다 -- 기구 실측으로 범위가
 * 넓어지거나 테이블 높이가 낮아지면 다시 도달 가능해진다. 이 사실 자체를
 * 회귀로 남겨두고, 중간경로 차단 메커니즘은 클램프 밖 값으로 직접
 * ForearmJointCommand를 만들어 계속 검증한다(apply()가 아니라
 * set_target()/step()을 직접 호출 -- 이 둘은 clamp를 하지 않는다).
 * 자기충돌(wrist_pitch>155)은 반대로 클램프 범위 안에서 도달 가능해졌다
 * -- test_forearm_safety_check.c의 test_self_collision 참고.
 */
static void test_clamped_envelope_never_reaches_table(void)
{
    float min_height = 1e9f;
    for (int roll = 20; roll <= 160; roll += 20)
    for (int pitch = 20; pitch <= 160; pitch += 20)
    for (int wp = 20; wp <= 160; wp += 20)
    for (int wr = 20; wr <= 160; wr += 20) {
        ForearmJointCommand c = raw_command((float)roll, (float)pitch, (float)wp, (float)wr);
        ForearmJointPositions3D p;
        assert(forearm_robot_forward_kinematics_3d(&c, &p));
        if (p.wrist.z_cm < min_height) min_height = p.wrist.z_cm;
        if (p.tip.z_cm < min_height) min_height = p.tip.z_cm;
    }
    assert(min_height > -5.0f); /* 지금 범위에서는 항상 테이블 위 */
}

/*
 * motion.c(Motion/SPEED_ACCEL) 도입 후 재작성(2026-09-23). 예전에는 pitch=170
 * (클램프 밖) 값으로 [20,160] 안에서는 도달 불가능한 테이블충돌 구간을 직접
 * 뚫고 지나가며 차단을 검증했는데, forearm_calibration_set_target()이 이제
 * motion_init()에 넘기기 전에 값을 [min,max]로 clamp한다(motion_init은 범위
 * 밖 initial을 그냥 거부하고 그 축의 Motion을 0으로 방치하므로, set_target()도
 * 항상 clamp하는 motion_set_target()과 동작을 맞추기 위해서다 -- 위
 * forearm_calibration_set_target() 주석 참고). 그래서 클램프 밖 값을 이 API에
 * 직접 넣는 시나리오 자체가 더 이상 의미가 없다. 대신 [20,160] 안에서 실제로
 * 도달 가능한 자기충돌(wrist_pitch>155, test_forearm_safety_check.c의
 * test_self_collision 참고)로 같은 차단 메커니즘을 검증한다.
 *
 * 실행해서 확인한 값(never guessed): roll=90,pitch=90,wr=90 고정, wp를
 * 20->160으로 보내면 tick=230에서 wp=154.544도까지 갔을 때 자기충돌로
 * 차단되고(blocked_flags=SELF_COLLISION), 그 뒤로는 새 목표가 오기 전까지
 * 그 자리에 멈춘다(차단은 경계를 처음 넘는 그 순간 한 번만 뜬다 -- 그 다음
 * 틱부터는 emergency_hold가 target=q로 얼어붙여서 "직전과 같은 안전한 위치"를
 * 다시 제안하므로 blocked_flags는 OK로 돌아간다. 이게 예전 설계와 다른 점:
 * 예전엔 ticks_elapsed만 멈추고 target은 안 바뀌어서 매틱 다시 막혔다).
 */
static void test_safe_endpoints_do_not_allow_unsafe_ramp(void)
{
    ForearmMotionState s;
    ForearmJointCommand start=raw_command(90,90,20,90);
    ForearmJointCommand end=raw_command(90,90,160,90);
    ForearmJointCommand output=start, previous;
    int blocked=0;
    forearm_calibration_state_init(&s);
    forearm_calibration_set_target(&s,&start);
    forearm_calibration_set_target(&s,&end);
    for (int tick=0; tick<400; tick++) {
        previous=output;
        forearm_calibration_step(&s,&output);
        assert(output.valid && forearm_safety_check_apply(&output,NULL));
        assert(fabsf(output.wrist_pitch_deg-previous.wrist_pitch_deg)<=0.6001f);
        if (s.blocked_flags) {
            assert(s.blocked_flags==FOREARM_SAFETY_CHECK_SELF_COLLISION);
            near(output.wrist_pitch_deg,previous.wrist_pitch_deg);
            blocked++;
        }
    }
    assert(blocked>0 && output.wrist_pitch_deg<155.0f);
    near(output.wrist_pitch_deg,154.544f);
    /* A safe new goal must release the hold, with no skipped trajectory time. */
    end=raw_command(90,90,20,20);
    forearm_calibration_set_target(&s,&end);
    assert(s.blocked_flags==0);
    for (int tick=0; tick<250; tick++) {
        previous=output;
        forearm_calibration_step(&s,&output);
        assert(forearm_safety_check_apply(&output,NULL));
        assert(s.blocked_flags==0);
        assert(fabsf(output.wrist_pitch_deg-previous.wrist_pitch_deg)<=0.6001f);
        assert(fabsf(output.wrist_roll_deg-previous.wrist_roll_deg)<=0.6001f);
    }
    near(output.wrist_pitch_deg,end.wrist_pitch_deg);
    near(output.wrist_roll_deg,end.wrist_roll_deg);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_validate();
    test_map_and_limits();
    test_unwrap();
    test_ramp_speed_and_retarget();
    test_clamped_envelope_never_reaches_table();
    test_safe_endpoints_do_not_allow_unsafe_ramp();
    puts("test_forearm_calibration: PASS (validate, mapping, limits, unwrap, ramp speed/retarget)");
    return 0;
}
