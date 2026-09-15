#include <stdio.h>
#include <stdint.h>

#include "common/robot_types.h"
#include "output_controller/output_control.h"
#include "output_controller/motion_record.h"


/*
 * ============================================================
 * Test Result
 * ============================================================
 */

static int test_fail_count = 0;


/*
 * ============================================================
 * Test Joint Commands
 * ============================================================
 *
 * A / B / C / D는 실제 기능 이름이 아니라
 * Testbench에서 사용하는 서로 다른 6축 자세이다.
 */

static const JointCommand CMD_A = {
    .base_deg        = 45.0f,
    .shoulder_deg    = 90.0f,
    .elbow_deg       = 135.0f,
    .wrist_pitch_deg = 45.0f,
    .wrist_roll_deg  = 90.0f,
    .gripper_norm    = 0.0f,
    .valid           = 1U
};


static const JointCommand CMD_B = {
    .base_deg        = 90.0f,
    .shoulder_deg    = 135.0f,
    .elbow_deg       = 45.0f,
    .wrist_pitch_deg = 90.0f,
    .wrist_roll_deg  = 135.0f,
    .gripper_norm    = 0.5f,
    .valid           = 1U
};


static const JointCommand CMD_C = {
    .base_deg        = 135.0f,
    .shoulder_deg    = 45.0f,
    .elbow_deg       = 90.0f,
    .wrist_pitch_deg = 135.0f,
    .wrist_roll_deg  = 45.0f,
    .gripper_norm    = 1.0f,
    .valid           = 1U
};


/*
 * 6축 값을 최대한 서로 다르게 둔 Test Command
 *
 * 예상 PWM:
 *
 * Base          22.5  -> 1125 us
 * Shoulder      45    -> 1250 us
 * Elbow         67.5  -> 1375 us
 * Wrist Pitch   112.5 -> 1625 us
 * Wrist Roll    157.5 -> 1875 us
 * Gripper       0.25  -> 1250 us
 */
static const JointCommand CMD_D = {
    .base_deg        = 22.5f,
    .shoulder_deg    = 45.0f,
    .elbow_deg       = 67.5f,
    .wrist_pitch_deg = 112.5f,
    .wrist_roll_deg  = 157.5f,
    .gripper_norm    = 0.25f,
    .valid           = 1U
};


static const JointCommand CMD_INVALID = {
    .base_deg        = 90.0f,
    .shoulder_deg    = 90.0f,
    .elbow_deg       = 90.0f,
    .wrist_pitch_deg = 90.0f,
    .wrist_roll_deg  = 90.0f,
    .gripper_norm    = 0.5f,

    .valid           = 0U
};


/*
 * ============================================================
 * Utility Functions
 * ============================================================
 */

static void check_u32(
    const char *name,
    uint32_t actual,
    uint32_t expected
)
{
    if (actual == expected) {

        printf(
            "[PASS] %-40s actual=%u expected=%u\n",
            name,
            (unsigned int)actual,
            (unsigned int)expected
        );
    }
    else {

        printf(
            "[FAIL] %-40s actual=%u expected=%u\n",
            name,
            (unsigned int)actual,
            (unsigned int)expected
        );

        test_fail_count++;
    }
}


static void check_mode(
    const char *name,
    RobotMode actual,
    RobotMode expected
)
{
    if (actual == expected) {

        printf(
            "[PASS] %-40s mode=%d\n",
            name,
            (int)actual
        );
    }
    else {

        printf(
            "[FAIL] %-40s actual=%d expected=%d\n",
            name,
            (int)actual,
            (int)expected
        );

        test_fail_count++;
    }
}


static uint8_t pwm_matches(
    const ServoPwmCommand *pwm,
    uint16_t base,
    uint16_t shoulder,
    uint16_t elbow,
    uint16_t wrist_pitch,
    uint16_t wrist_roll,
    uint16_t gripper
)
{
    if (pwm->base_pwm_us != base)
        return 0U;

    if (pwm->shoulder_pwm_us != shoulder)
        return 0U;

    if (pwm->elbow_pwm_us != elbow)
        return 0U;

    if (pwm->wrist_pitch_pwm_us != wrist_pitch)
        return 0U;

    if (pwm->wrist_roll_pwm_us != wrist_roll)
        return 0U;

    if (pwm->gripper_pwm_us != gripper)
        return 0U;

    return 1U;
}


static void check_pwm(
    const char *name,
    const ServoPwmCommand *pwm,
    uint16_t base,
    uint16_t shoulder,
    uint16_t elbow,
    uint16_t wrist_pitch,
    uint16_t wrist_roll,
    uint16_t gripper
)
{
    if (pwm_matches(
            pwm,
            base,
            shoulder,
            elbow,
            wrist_pitch,
            wrist_roll,
            gripper)) {

        printf(
            "[PASS] %s\n",
            name
        );
    }
    else {

        printf(
            "[FAIL] %s\n",
            name
        );

        printf(
            "       BASE          actual=%u expected=%u\n",
            pwm->base_pwm_us,
            base
        );

        printf(
            "       SHOULDER      actual=%u expected=%u\n",
            pwm->shoulder_pwm_us,
            shoulder
        );

        printf(
            "       ELBOW         actual=%u expected=%u\n",
            pwm->elbow_pwm_us,
            elbow
        );

        printf(
            "       WRIST_PITCH   actual=%u expected=%u\n",
            pwm->wrist_pitch_pwm_us,
            wrist_pitch
        );

        printf(
            "       WRIST_ROLL    actual=%u expected=%u\n",
            pwm->wrist_roll_pwm_us,
            wrist_roll
        );

        printf(
            "       GRIPPER       actual=%u expected=%u\n",
            pwm->gripper_pwm_us,
            gripper
        );

        test_fail_count++;
    }
}


/*
 * ============================================================
 * TEST 1
 * 6축 JointCommand가 한 번에 변환되는지
 * ============================================================
 */

static void test_six_axis_bundle(void)
{
    ServoPwmCommand pwm_cmd;

    uint8_t result;


    printf("\n");
    printf("========================================\n");
    printf(" TEST 1 : SIX AXIS COMMAND\n");
    printf("========================================\n");


    output_control_init();


    result =
        output_control_update(
            &CMD_D,
            &pwm_cmd
        );


    check_u32(
        "6-axis conversion result",
        result,
        1U
    );


    check_pwm(
        "6-axis PWM bundle",
        &pwm_cmd,

        1125,
        1250,
        1375,
        1625,
        1875,
        1250
    );
}


/*
 * ============================================================
 * TEST 2
 * valid = 0
 *
 * -> PWM 생성 X
 * -> Record 저장 X
 * ============================================================
 */

static void test_invalid_command(void)
{
    ServoPwmCommand pwm_cmd = {
        .base_pwm_us        = 1111U,
        .shoulder_pwm_us    = 1222U,
        .elbow_pwm_us       = 1333U,
        .wrist_pitch_pwm_us = 1444U,
        .wrist_roll_pwm_us  = 1555U,
        .gripper_pwm_us     = 1666U
    };

    uint8_t result;


    printf("\n");
    printf("========================================\n");
    printf(" TEST 2 : INVALID COMMAND\n");
    printf("========================================\n");


    output_control_init();


    /*
     * RECORD ON
     */
    output_control_record_button_isr();


    result =
        output_control_update(
            &CMD_INVALID,
            &pwm_cmd
        );


    /*
     * valid = 0이므로 새로운 PWM 출력 없음
     */
    check_u32(
        "Invalid command output rejected",
        result,
        0U
    );


    /*
     * Record Buffer에도 저장되면 안 됨
     */
    check_u32(
        "Invalid command not recorded",
        output_control_get_record_count(),
        0U
    );


    /*
     * PWM 값도 변경되지 않아야 함
     */
    check_pwm(
        "PWM unchanged after invalid command",
        &pwm_cmd,

        1111,
        1222,
        1333,
        1444,
        1555,
        1666
    );
}


/*
 * ============================================================
 * TEST 3
 * 녹화 데이터 없이 MODE 버튼
 *
 * -> PLAYBACK 진입 X
 * -> FOLLOW 유지
 * ============================================================
 */

static void test_empty_playback(void)
{
    ServoPwmCommand pwm_cmd;

    uint8_t result;


    printf("\n");
    printf("========================================\n");
    printf(" TEST 3 : EMPTY PLAYBACK\n");
    printf("========================================\n");


    output_control_init();


    /*
     * 녹화 데이터가 하나도 없는 상태에서
     * MODE 버튼 누름
     */
    output_control_mode_button_isr();


    result =
        output_control_update(
            &CMD_A,
            &pwm_cmd
        );


    /*
     * PLAYBACK으로 가면 안 됨
     */
    check_mode(
        "Remain FOLLOW with no record",
        output_control_get_mode(),
        ROBOT_MODE_FOLLOW
    );


    /*
     * FOLLOW이므로 live CMD_A는 정상 출력
     */
    check_u32(
        "Live command still works",
        result,
        1U
    );


    check_pwm(
        "FOLLOW CMD_A",
        &pwm_cmd,

        1250,
        1500,
        1750,
        1250,
        1500,
        1000
    );
}


/*
 * ============================================================
 * TEST 4
 * PLAYBACK 중 RECORD 버튼
 *
 * -> Recording 시작 X
 * ============================================================
 */

static void test_record_ignored_during_playback(void)
{
    ServoPwmCommand pwm_cmd;

    uint8_t result;


    printf("\n");
    printf("========================================\n");
    printf(" TEST 4 : RECORD DURING PLAYBACK\n");
    printf("========================================\n");


    output_control_init();


    /*
     * A / B 녹화
     */
    output_control_record_button_isr();

    output_control_update(
        &CMD_A,
        &pwm_cmd
    );

    output_control_update(
        &CMD_B,
        &pwm_cmd
    );


    /*
     * RECORD OFF
     */
    output_control_record_button_isr();

    output_control_update(
        &CMD_C,
        &pwm_cmd
    );


    check_u32(
        "Recorded command count",
        output_control_get_record_count(),
        2U
    );


    /*
     * PLAYBACK 시작
     */
    output_control_mode_button_isr();


    result =
        output_control_update(
            &CMD_C,
            &pwm_cmd
        );


    check_mode(
        "Entered PLAYBACK",
        output_control_get_mode(),
        ROBOT_MODE_PLAYBACK
    );


    /*
     * 첫 번째 Playback = A
     */
    check_pwm(
        "Playback first command A",
        &pwm_cmd,

        1250,
        1500,
        1750,
        1250,
        1500,
        1000
    );


    /*
     * PLAYBACK 중 RECORD 버튼 누름
     */
    output_control_record_button_isr();


    /*
     * 두 번째 Playback = B
     */
    result =
        output_control_update(
            &CMD_C,
            &pwm_cmd
        );


    check_u32(
        "Playback continues",
        result,
        1U
    );


    check_u32(
        "Recording remains OFF",
        output_control_is_recording(),
        0U
    );


    check_pwm(
        "Playback second command B",
        &pwm_cmd,

        1500,
        1750,
        1250,
        1500,
        1750,
        1500
    );
}


/*
 * ============================================================
 * TEST 5
 * 새 RECORD 시작
 *
 * 기존 A/B/C 삭제
 * -> 새 D만 저장
 * ============================================================
 */

static void test_new_record_clears_old_data(void)
{
    ServoPwmCommand pwm_cmd;

    uint8_t result;


    printf("\n");
    printf("========================================\n");
    printf(" TEST 5 : NEW RECORD CLEARS OLD DATA\n");
    printf("========================================\n");


    output_control_init();


    /*
     * 첫 번째 Record
     *
     * A / B / C 저장
     */
    output_control_record_button_isr();

    output_control_update(
        &CMD_A,
        &pwm_cmd
    );

    output_control_update(
        &CMD_B,
        &pwm_cmd
    );

    output_control_update(
        &CMD_C,
        &pwm_cmd
    );


    /*
     * Record OFF
     */
    output_control_record_button_isr();

    output_control_update(
        &CMD_A,
        &pwm_cmd
    );


    check_u32(
        "Old record count = 3",
        output_control_get_record_count(),
        3U
    );


    /*
     * 새로운 Record 시작
     *
     * 여기서 기존 A/B/C가 clear 되어야 함
     */
    output_control_record_button_isr();

    output_control_update(
        &CMD_D,
        &pwm_cmd
    );


    check_u32(
        "New record count reset to 1",
        output_control_get_record_count(),
        1U
    );


    /*
     * Record OFF
     */
    output_control_record_button_isr();

    output_control_update(
        &CMD_A,
        &pwm_cmd
    );


    /*
     * Playback 시작
     */
    output_control_mode_button_isr();


    result =
        output_control_update(
            NULL,
            &pwm_cmd
        );


    check_u32(
        "New record playback result",
        result,
        1U
    );


    /*
     * 이전 A가 아니라 D가 나와야 한다.
     */
    check_pwm(
        "Only new CMD_D remains",
        &pwm_cmd,

        1125,
        1250,
        1375,
        1625,
        1875,
        1250
    );


    /*
     * D 한 개만 저장했으므로
     * 다음 데이터는 없어야 함
     */
    result =
        output_control_update(
            NULL,
            &pwm_cmd
        );


    check_u32(
        "Old A/B/C removed",
        result,
        0U
    );
}


/*
 * ============================================================
 * TEST 6
 * Record Buffer Full
 *
 * 1800개 저장
 * -> 추가 저장 실패
 * -> 기존 데이터 유지
 * ============================================================
 */

static void test_record_buffer_full(void)
{
    ServoPwmCommand pwm_cmd;

    uint32_t capacity;
    uint32_t i;

    uint8_t result;
    uint8_t append_result;
    uint8_t playback_ok = 1U;


    printf("\n");
    printf("========================================\n");
    printf(" TEST 6 : RECORD BUFFER FULL\n");
    printf("========================================\n");


    output_control_init();


    capacity =
        motion_record_get_capacity();


    check_u32(
        "Record buffer capacity",
        capacity,
        MOTION_RECORD_MAX_FRAMES
    );


    /*
     * RECORD ON
     */
    output_control_record_button_isr();


    /*
     * 첫 번째 데이터는 A
     */
    output_control_update(
        &CMD_A,
        &pwm_cmd
    );


    /*
     * 나머지는 B로 채운다.
     */
    for (i = 1U; i < capacity; i++) {

        output_control_update(
            &CMD_B,
            &pwm_cmd
        );
    }


    check_u32(
        "Buffer reached full capacity",
        output_control_get_record_count(),
        capacity
    );


    /*
     * Buffer가 Full인 상태에서
     * 추가 저장 시도
     *
     * motion_record_append()는 실패해야 한다.
     */
    append_result =
        motion_record_append(
            &CMD_C
        );


    check_u32(
        "Extra append rejected",
        append_result,
        0U
    );


    check_u32(
        "Record count remains full",
        output_control_get_record_count(),
        capacity
    );


    /*
     * Output Controller를 통해서도 추가 CMD_C를 넣어본다.
     *
     * Servo 출력 자체는 정상적으로 수행되지만
     * Record Count는 증가하면 안 된다.
     */
    result =
        output_control_update(
            &CMD_C,
            &pwm_cmd
        );


    check_u32(
        "Servo output still works when buffer full",
        result,
        1U
    );


    check_u32(
        "Buffer does not exceed capacity",
        output_control_get_record_count(),
        capacity
    );


    /*
     * RECORD OFF
     */
    output_control_record_button_isr();

    output_control_update(
        &CMD_C,
        &pwm_cmd
    );


    /*
     * PLAYBACK 시작
     */
    output_control_mode_button_isr();


    /*
     * 첫 데이터는 반드시 A여야 한다.
     */
    result =
        output_control_update(
            NULL,
            &pwm_cmd
        );


    check_u32(
        "Full buffer playback starts",
        result,
        1U
    );


    check_pwm(
        "First stored command still A",
        &pwm_cmd,

        1250,
        1500,
        1750,
        1250,
        1500,
        1000
    );


    /*
     * 나머지 capacity - 1개는 전부 B여야 한다.
     *
     * Buffer Full 이후 넣은 C가
     * 기존 Record를 덮어쓰지 않았는지 확인.
     */
    for (i = 1U; i < capacity; i++) {

        result =
            output_control_update(
                NULL,
                &pwm_cmd
            );


        if (result != 1U) {

            playback_ok = 0U;
            break;
        }


        if (!pwm_matches(
                &pwm_cmd,

                1500,
                1750,
                1250,
                1500,
                1750,
                1500)) {

            playback_ok = 0U;
            break;
        }
    }


    check_u32(
        "Stored data not corrupted",
        playback_ok,
        1U
    );


    /*
     * 모든 데이터 읽은 후
     * 더 이상 출력되면 안 된다.
     */
    result =
        output_control_update(
            NULL,
            &pwm_cmd
        );


    check_u32(
        "No data after full playback",
        result,
        0U
    );
}


/*
 * ============================================================
 * TEST 7
 * Playback 종료 이후
 *
 * -> 새로운 Command 없음
 * -> PLAYBACK Mode는 유지
 * ============================================================
 */

static void test_playback_end(void)
{
    ServoPwmCommand pwm_cmd;

    uint8_t result;


    printf("\n");
    printf("========================================\n");
    printf(" TEST 7 : PLAYBACK END\n");
    printf("========================================\n");


    output_control_init();


    /*
     * CMD_D 하나만 Record
     */
    output_control_record_button_isr();

    output_control_update(
        &CMD_D,
        &pwm_cmd
    );


    /*
     * RECORD OFF
     */
    output_control_record_button_isr();

    output_control_update(
        &CMD_A,
        &pwm_cmd
    );


    /*
     * PLAYBACK 시작
     */
    output_control_mode_button_isr();


    /*
     * 첫 번째 = D
     */
    result =
        output_control_update(
            NULL,
            &pwm_cmd
        );


    check_u32(
        "Last recorded command output",
        result,
        1U
    );


    check_pwm(
        "Playback CMD_D",
        &pwm_cmd,

        1125,
        1250,
        1375,
        1625,
        1875,
        1250
    );


    /*
     * 다음 데이터 없음
     */
    result =
        output_control_update(
            NULL,
            &pwm_cmd
        );


    check_u32(
        "Output stops after playback end",
        result,
        0U
    );


    /*
     * Playback이 끝나도 Mode 자체는 PLAYBACK 유지
     */
    check_mode(
        "Mode remains PLAYBACK",
        output_control_get_mode(),
        ROBOT_MODE_PLAYBACK
    );


    /*
     * MODE 버튼 다시 누르면 FOLLOW
     */
    output_control_mode_button_isr();


    result =
        output_control_update(
            &CMD_A,
            &pwm_cmd
        );


    check_mode(
        "Return to FOLLOW",
        output_control_get_mode(),
        ROBOT_MODE_FOLLOW
    );


    check_u32(
        "Live command works again",
        result,
        1U
    );
}


/*
 * ============================================================
 * Main
 * ============================================================
 */

int main(void)
{
    printf("\n");
    printf("========================================\n");
    printf(" OUTPUT CONTROLLER EXCEPTION TEST START\n");
    printf("========================================\n");


    test_six_axis_bundle();

    test_invalid_command();

    test_empty_playback();

    test_record_ignored_during_playback();

    test_new_record_clears_old_data();

    test_record_buffer_full();

    test_playback_end();


    printf("\n");
    printf("========================================\n");

    if (test_fail_count == 0) {

        printf(" ALL TESTS PASSED\n");
    }
    else {

        printf(
            " TEST FAILED : %d error(s)\n",
            test_fail_count
        );
    }

    printf("========================================\n\n");


    return test_fail_count;
}