#include "output_controller/output_control.h"

#include "output_controller/motion_record.h"
#include "output_controller/robot_mode.h"
#include "output_controller/servo_control.h"

#include <stddef.h>


/*
 * ============================================================
 * Previous State
 * ============================================================
 *
 * Record / Mode가 "변경되는 순간"을 검출하기 위해
 * 이전 상태를 저장한다.
 *
 * 예:
 *
 * previous_recording = 0
 * current_recording  = 1
 *
 * -> Record 시작 순간
 *
 *
 * Verilog로 비유하면 이전 state를 저장하는 register.
 */
static RobotMode previous_mode =
    ROBOT_MODE_FOLLOW;

static uint8_t previous_recording = 0U;


/*
 * ============================================================
 * Initialization
 * ============================================================
 */
void output_control_init(void)
{
    /*
     * 하위 제어 모듈 초기화
     */
    robot_mode_init();

    motion_record_init();


    /*
     * 초기 상태 저장
     *
     * 기본 Mode:
     *   FOLLOW
     *
     * 기본 Record:
     *   OFF
     */
    previous_mode =
        ROBOT_MODE_FOLLOW;

    previous_recording =
        0U;
}


/*
 * ============================================================
 * RECORD Button ISR
 * ============================================================
 *
 * 실제 Record 동작은 하지 않는다.
 *
 * Event만 robot_mode에 전달하고
 * 즉시 ISR을 종료하도록 한다.
 */
void output_control_record_button_isr(void)
{
    robot_mode_notify_record_button_isr();
}


/*
 * ============================================================
 * MODE Button ISR
 * ============================================================
 */
void output_control_mode_button_isr(void)
{
    robot_mode_notify_mode_button_isr();
}


/*
 * ============================================================
 * Main Output Control Update
 * ============================================================
 */
uint8_t output_control_update(
    const JointCommand *live_cmd,
    ServoPwmCommand *pwm_cmd
)
{
    RobotMode current_mode;

    uint8_t current_recording;

    JointCommand selected_cmd;


    /*
     * --------------------------------------------------------
     * Output Pointer Check
     * --------------------------------------------------------
     */
    if (pwm_cmd == NULL) {
        return 0U;
    }


    /*
     * --------------------------------------------------------
     * 1. Button Event 처리
     * --------------------------------------------------------
     *
     * 저장된 Motion이 있는지 robot_mode에 알려준다.
     *
     * 이를 통해:
     *
     * 저장 데이터 없음
     * -> PLAYBACK 진입 방지
     */
    robot_mode_process_events(
        motion_record_has_data()
    );


    /*
     * --------------------------------------------------------
     * 2. 현재 Mode / Record 상태 읽기
     * --------------------------------------------------------
     */
    current_mode =
        robot_mode_get();

    current_recording =
        robot_mode_is_recording();


    /*
     * --------------------------------------------------------
     * 3. Record 시작 Edge 검출
     * --------------------------------------------------------
     *
     * OFF -> ON
     *
     * 이 순간 기존 Record 내용을 삭제한다.
     *
     * 중요:
     * 매 Control Cycle마다 clear하면 안 된다.
     *
     * Record가 시작되는 순간 딱 한 번만 실행한다.
     */
    if ((previous_recording == 0U) &&
        (current_recording != 0U)) {

        motion_record_clear();
    }


    /*
     * --------------------------------------------------------
     * 4. Playback 시작 Edge 검출
     * --------------------------------------------------------
     *
     * FOLLOW -> PLAYBACK
     *
     * 이 순간 Playback Read Index를
     * 첫 번째 Command로 초기화한다.
     */
    if ((previous_mode != ROBOT_MODE_PLAYBACK) &&
        (current_mode == ROBOT_MODE_PLAYBACK)) {

        motion_record_playback_start();
    }


    /*
     * --------------------------------------------------------
     * 5. Mode에 따라 사용할 JointCommand 선택
     * --------------------------------------------------------
     */


    /*
     * ========================================================
     * FOLLOW MODE
     * ========================================================
     */
    if (current_mode == ROBOT_MODE_FOLLOW) {

        /*
         * FOLLOW에서는 Agent2의 실시간 Command가 필요하다.
         */
        if (live_cmd == NULL) {

            previous_mode =
                current_mode;

            previous_recording =
                current_recording;

            return 0U;
        }


        /*
         * Invalid JointCommand 사용 금지
         */
        if (!live_cmd->valid) {

            previous_mode =
                current_mode;

            previous_recording =
                current_recording;

            return 0U;
        }


        /*
         * Servo에 보낼 Command 선택
         */
        selected_cmd =
            *live_cmd;


        /*
         * RECORD가 ON이면
         * 같은 JointCommand를 Record Buffer에도 저장한다.
         *
         * 즉:
         *
         *              ┌-> Servo
         * JointCommand |
         *              └-> Record Buffer
         */
        if (current_recording) {

            motion_record_append(
                live_cmd
            );
        }
    }


    /*
     * ========================================================
     * PLAYBACK MODE
     * ========================================================
     */
    else {

        /*
         * 저장된 다음 JointCommand를 읽는다.
         */
        if (!motion_record_playback_next(
                &selected_cmd)) {

            /*
             * Playback이 끝났으면
             * 새로운 PWM Command를 생성하지 않는다.
             *
             * 실제 PWM HW에서는 마지막 PWM 값이
             * 그대로 유지되는 구조로 사용할 예정이다.
             */
            previous_mode =
                current_mode;

            previous_recording =
                current_recording;

            return 0U;
        }
    }


    /*
     * --------------------------------------------------------
     * 6. JointCommand -> Servo PWM
     * --------------------------------------------------------
     */
    if (!servo_control_convert(
            &selected_cmd,
            pwm_cmd)) {

        previous_mode =
            current_mode;

        previous_recording =
            current_recording;

        return 0U;
    }


    /*
     * --------------------------------------------------------
     * 7. 현재 상태를 다음 Cycle용 Previous State로 저장
     * --------------------------------------------------------
     */
    previous_mode =
        current_mode;

    previous_recording =
        current_recording;


    return 1U;
}


/*
 * ============================================================
 * Current Robot Mode
 * ============================================================
 */
RobotMode output_control_get_mode(void)
{
    return robot_mode_get();
}


/*
 * ============================================================
 * Current Recording State
 * ============================================================
 */
uint8_t output_control_is_recording(void)
{
    return robot_mode_is_recording();
}


/*
 * ============================================================
 * Current Record Count
 * ============================================================
 */
uint32_t output_control_get_record_count(void)
{
    return motion_record_get_count();
}