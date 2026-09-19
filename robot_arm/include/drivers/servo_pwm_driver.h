#ifndef DRIVERS_SERVO_PWM_DRIVER_H
#define DRIVERS_SERVO_PWM_DRIVER_H

#include <stdint.h>


/*
 * ============================================================
 * Servo PWM Driver Channel
 * ============================================================
 *
 * Robot Joint 기준 Channel.
 *
 * 실제 RTL Register 이름과 일부 이름이 다르지만
 * Software에서는 Robot Joint 기준으로 사용한다.
 */
typedef enum {

    SERVO_PWM_DRIVER_BASE = 0,
    SERVO_PWM_DRIVER_SHOULDER,
    SERVO_PWM_DRIVER_ELBOW,
    SERVO_PWM_DRIVER_WRIST_PITCH,
    SERVO_PWM_DRIVER_WRIST_ROLL,
    SERVO_PWM_DRIVER_GRIPPER,

    SERVO_PWM_DRIVER_CHANNEL_COUNT

} ServoPwmDriverChannel;


/*
 * ============================================================
 * Driver Initialization
 * ============================================================
 */
void servo_pwm_driver_init(void);


/*
 * ============================================================
 * PWM Output Enable / Disable
 * ============================================================
 *
 * return:
 *
 * 1 = success
 * 0 = fail
 */
int servo_pwm_driver_enable(void);

int servo_pwm_driver_disable(void);


/*
 * ============================================================
 * Servo Channel Shadow Register Write
 * ============================================================
 *
 * pwm_us:
 *   microsecond(us)
 *
 * 이 함수에서는 UPDATE를 발생시키지 않는다.
 */
int servo_pwm_driver_write_channel(
    ServoPwmDriverChannel channel,
    uint16_t pwm_us
);


/*
 * ============================================================
 * UPDATE
 * ============================================================
 *
 * 6개의 Shadow Register 값을
 * RTL Active 값으로 적용한다.
 *
 * return:
 *
 * 1 = success
 * 0 = fail
 */
int servo_pwm_driver_update(void);


#ifndef SERVO_PWM_DRIVER_USE_XILINX

/*
 * ============================================================
 * HOST / WSL Mock Test API
 * ============================================================
 *
 * 실제 Zybo/Vitis에서는 컴파일되지 않는다.
 */

typedef struct {

    uint32_t offset;
    uint32_t value;

} ServoPwmDriverMockWrite;


/*
 * Mock Register / Write Log 전체 초기화.
 *
 * 호출 후 Driver도 uninitialized 상태가 된다.
 * 테스트에서는:
 *
 * servo_pwm_driver_mock_reset();
 * servo_pwm_driver_init();
 *
 * 순서로 사용한다.
 */
void servo_pwm_driver_mock_reset(void);


/*
 * 현재 Write Log 개수.
 */
uint32_t servo_pwm_driver_mock_get_log_count(void);


/*
 * 특정 Write Log 조회.
 *
 * return:
 *
 * 1 = success
 * 0 = invalid index / NULL pointer
 */
int servo_pwm_driver_mock_get_log(
    uint32_t index,
    ServoPwmDriverMockWrite *entry
);

#endif


#endif /* DRIVERS_SERVO_PWM_DRIVER_H */