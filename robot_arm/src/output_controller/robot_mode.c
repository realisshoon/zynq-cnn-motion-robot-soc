#include "output_controller/robot_mode.h"


/*
 * 현재 Robot 동작 Mode
 *
 * 초기값:
 * FOLLOW
 */
static RobotMode current_mode =
    ROBOT_MODE_FOLLOW;


/*
 * Recording Enable 상태
 *
 * 0 = OFF
 * 1 = ON
 */
static uint8_t recording_enabled = 0;


/*
 * Interrupt Event Flags
 *
 * volatile:
 *   일반 코드가 아닌 Interrupt에서도
 *   값이 변경될 수 있음을 Compiler에게 알린다.
 */
static volatile uint8_t record_button_event = 0;
static volatile uint8_t mode_button_event = 0;


/*
 * Robot Mode 초기화
 */
void robot_mode_init(void)
{
    current_mode = ROBOT_MODE_FOLLOW;

    recording_enabled = 0;

    record_button_event = 0;
    mode_button_event = 0;
}


/*
 * RECORD Button ISR Event
 *
 * Interrupt 안에서는 Event만 남긴다.
 */
void robot_mode_notify_record_button_isr(void)
{
    record_button_event = 1;
}


/*
 * MODE Button ISR Event
 *
 * Interrupt 안에서는 Event만 남긴다.
 */
void robot_mode_notify_mode_button_isr(void)
{
    mode_button_event = 1;
}


/*
 * Button Event 처리
 *
 * Main Control Loop에서 호출한다.
 */
void robot_mode_process_events(
    uint8_t has_recorded_motion
)
{
    /*
     * ========================================
     * MODE Button
     * ========================================
     *
     * MODE 버튼은 동작 Mode를 전환한다.
     *
     * FOLLOW -> PLAYBACK
     * PLAYBACK -> FOLLOW
     *
     * Mode 변경을 먼저 처리한다.
     */
    if (mode_button_event) {

        mode_button_event = 0;


        /*
         * 현재 FOLLOW Mode인 경우
         */
        if (current_mode == ROBOT_MODE_FOLLOW) {

            /*
             * Playback으로 이동할 때
             * Recording이 켜져 있다면 자동 종료.
             */
            recording_enabled = 0;


            /*
             * 저장된 Motion이 있을 때만
             * PLAYBACK Mode 진입.
             */
            if (has_recorded_motion) {

                current_mode =
                    ROBOT_MODE_PLAYBACK;
            }
        }


        /*
         * 현재 PLAYBACK Mode인 경우
         *
         * 다시 버튼을 누르면
         * FOLLOW Mode로 복귀.
         */
        else {

            current_mode =
                ROBOT_MODE_FOLLOW;
        }
    }


    /*
     * ========================================
     * RECORD Button
     * ========================================
     *
     * FOLLOW Mode에서만 사용할 수 있다.
     *
     * 버튼을 누를 때마다:
     *
     * OFF -> ON
     * ON  -> OFF
     */
    if (record_button_event) {

        record_button_event = 0;


        /*
         * PLAYBACK 중에는
         * Record Button을 무시한다.
         */
        if (current_mode ==
            ROBOT_MODE_FOLLOW) {

            recording_enabled =
                !recording_enabled;
        }
    }
}


/*
 * 현재 Mode 반환
 */
RobotMode robot_mode_get(void)
{
    return current_mode;
}


/*
 * 현재 Recording 상태 반환
 */
uint8_t robot_mode_is_recording(void)
{
    return recording_enabled;
}