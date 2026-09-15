#ifndef OUTPUT_CONTROLLER_ROBOT_MODE_H
#define OUTPUT_CONTROLLER_ROBOT_MODE_H

#include <stdint.h>


/*
 * Robot Operation Mode
 *
 * FOLLOW:
 *   실시간 CNN 기반 JointCommand를 사용한다.
 *
 * PLAYBACK:
 *   motion_record에 저장된 JointCommand를 사용한다.
 */
typedef enum {
    ROBOT_MODE_FOLLOW = 0,
    ROBOT_MODE_PLAYBACK
} RobotMode;


/*
 * Robot Mode 초기화
 *
 * 초기 상태:
 *   Mode      = FOLLOW
 *   Recording = OFF
 */
void robot_mode_init(void);


/*
 * RECORD 버튼 Interrupt Event
 *
 * 실제 Record 동작을 ISR 안에서 수행하지 않는다.
 * ISR에서는 이 함수만 호출하여
 * 버튼 Event가 발생했음을 기록한다.
 */
void robot_mode_notify_record_button_isr(void);


/*
 * MODE 버튼 Interrupt Event
 *
 * ISR에서는 실제 Mode 전환을 수행하지 않고
 * Event 발생 사실만 기록한다.
 */
void robot_mode_notify_mode_button_isr(void);


/*
 * Button Event 처리
 *
 * Main Control Loop에서 호출한다.
 *
 * has_recorded_motion:
 *   0 = 저장된 Motion 없음
 *   1 = 재생 가능한 Motion 있음
 */
void robot_mode_process_events(
    uint8_t has_recorded_motion
);


/*
 * 현재 Robot Mode 반환
 */
RobotMode robot_mode_get(void);


/*
 * 현재 Recording 상태 반환
 *
 * 0 = Recording OFF
 * 1 = Recording ON
 */
uint8_t robot_mode_is_recording(void);


#endif /* OUTPUT_CONTROLLER_ROBOT_MODE_H */