#include "output_controller/output_control.h"
#include "output_controller/servo_hal.h"


int main(void)
{
    /*
     * ========================================================
     * Output Controller Initialization
     * ========================================================
     */
    output_control_init();


    /*
     * ========================================================
     * Servo HAL / Driver Initialization
     * ========================================================
     *
     * 여기서는 아직 PWM Enable 안 함.
     */
    servo_hal_init();


    /*
     * ========================================================
     * Startup Safe Pose
     * ========================================================
     *
     * 현재 초기값:
     *
     * BASE        = 1500 us
     * SHOULDER    = 1500 us
     * ELBOW       = 1500 us
     * WRIST_PITCH = 1500 us
     * WRIST_ROLL  = 1500 us
     * GRIPPER     = 1500 us
     *
     * 적용 순서:
     *
     * Shadow 6개
     *    ↓
     * UPDATE
     *    ↓
     * ENABLE
     */
    if (!servo_hal_startup()) {
        return 1;
    }


    /*
     * ========================================================
     * Main Control Loop
     * ========================================================
     *
     * 최종 Control Cycle:
     *
     * 20 ms = 50 Hz
     *
     * 실제 JointCommand 연동 및
     * 20 ms Timer는 Vitis 통합 단계에서 연결한다.
     */
    while (1) {

    }


    return 0;
}