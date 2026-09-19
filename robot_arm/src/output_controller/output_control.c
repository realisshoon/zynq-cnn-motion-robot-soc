#include "output_controller/output_control.h"


/*
 * ============================================================
 * Initialization
 * ============================================================
 */
void output_control_init(void)
{
    /*
     * 현재는 별도의 내부 상태가 없다.
     *
     * 추후 Output Enable / Emergency Stop 등의
     * 상태 관리가 필요할 경우 이곳에서 초기화한다.
     */
}


/*
 * ============================================================
 * Main Output Control Update
 * ============================================================
 */
uint8_t output_control_update(
    const JointCommand *joint_cmd,
    ServoPwmCommand *pwm_cmd
)
{
    /*
     * JointCommand -> ServoPwmCommand 변환.
     *
     * NULL / valid 확인은
     * servo_control_convert() 내부에서 처리한다.
     */
    return servo_control_convert(
        joint_cmd,
        pwm_cmd
    );
}