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
     * Five-channel startup PWM comes from servo_config.c.
     * Write five shadow registers, UPDATE, then ENABLE.
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
     * 실제 ForearmJointCommand 연동 및
     * 20 ms Timer는 Vitis 통합 단계에서 연결한다.
     */
    while (1) {

    }


    return 0;
}