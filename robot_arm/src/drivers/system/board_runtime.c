#include "drivers/uart/console_uart.h"
#include "firmware_config.h"


#include "drivers/system/platform.h"
#include "drivers/system/platform_vitis.h"

#include <stddef.h>
#include <stdint.h>

#include "drivers/pwm/servo_hal.h"

#include "xil_exception.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xscugic.h"
#include "xstatus.h"
#include "xtime_l.h"
#include "xtmrctr.h"

/* ---- 설정값 (XSA가 만든 xparameters.h 기준) ---- */
#define PLATFORM_GIC_DEVICE_ID    XPAR_SCUGIC_0_DEVICE_ID
#define PLATFORM_TIMER_DEVICE_ID  XPAR_AXI_TIMER_0_DEVICE_ID
#define PLATFORM_TIMER_IRQ_ID     XPAR_FABRIC_AXI_TIMER_0_INTERRUPT_INTR /* CNN XSA: IRQ 62 */

#define PLATFORM_TICK_MS          (ROBOT_CONTROL_PERIOD_US / 1000U)                                   /* 제어 주기 20ms = 50Hz */
/* 다운 카운트 자동 재적재 값: 100MHz x 20ms = 2,000,000. 실제 주기와의 차이는 수 클럭(수십 ns)이라 무시한다. */
#define PLATFORM_TIMER_RELOAD     ((XPAR_AXI_TIMER_0_CLOCK_FREQ_HZ / 1000U) * PLATFORM_TICK_MS)

#define PLATFORM_IRQ_PRIORITY     0xA0U /* CNN IRQ와 같은 우선순위. */
#define PLATFORM_IRQ_LEVEL_HIGH   0x1U  /* AXI Timer의 interrupt 출력은 레벨 하이다(XSA hwh에서 확인). */

static XScuGic s_gic;
static XTmrCtr s_timer;
/*
 * 틱 상태. s_tick_count는 ISR만 올리고 나머지는 main만 갱신한다.
 * 쓰는 쪽이 하나씩이라 잠금이 필요 없고, main은 카운터를 한 번만 읽어서 쓴다.
 */
static volatile uint32_t s_tick_count;
static uint32_t s_tick_seen;
static uint32_t s_tick_overruns; /* 한 틱보다 더 밀려서 버린 틱 수. 디버거로 확인한다. */
static int s_tick_armed;

/* XTmrCtr 드라이버 핸들러가 호출한다. 인터럽트 확인응답(플래그 해제)은 이 함수가 끝난 뒤 드라이버가 한다. */
static void tick_isr(void *ref, u8 timer_no)
{
    (void)ref;
    (void)timer_no;
    s_tick_count++;
}

/* 인터럽트 컨트롤러와 AXI Timer를 설정하고 타이머를 시작한다. 성공 0, 실패 -1. */
static int tick_timer_start(void)
{
    XScuGic_Config *gic_cfg;

    gic_cfg = XScuGic_LookupConfig(PLATFORM_GIC_DEVICE_ID);
    if (gic_cfg == NULL) return -1;
    if (XScuGic_CfgInitialize(&s_gic, gic_cfg, gic_cfg->CpuBaseAddress) != XST_SUCCESS) return -1;

    Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
                                 (Xil_ExceptionHandler)XScuGic_InterruptHandler, &s_gic);

    if (XTmrCtr_Initialize(&s_timer, PLATFORM_TIMER_DEVICE_ID) != XST_SUCCESS) return -1;
    XTmrCtr_SetHandler(&s_timer, tick_isr, &s_timer);
    XTmrCtr_SetOptions(&s_timer, 0U,
                       XTC_INT_MODE_OPTION | XTC_AUTO_RELOAD_OPTION | XTC_DOWN_COUNT_OPTION);
    XTmrCtr_SetResetValue(&s_timer, 0U, PLATFORM_TIMER_RELOAD);

    XScuGic_SetPriorityTriggerType(&s_gic, PLATFORM_TIMER_IRQ_ID,
                                   PLATFORM_IRQ_PRIORITY, PLATFORM_IRQ_LEVEL_HIGH);
    if (XScuGic_Connect(&s_gic, PLATFORM_TIMER_IRQ_ID,
                        (Xil_ExceptionHandler)XTmrCtr_InterruptHandler, &s_timer) != XST_SUCCESS) {
        return -1;
    }
    XScuGic_Enable(&s_gic, PLATFORM_TIMER_IRQ_ID);

    /* 설정을 모두 마친 뒤에 IRQ를 열고, 타이머는 맨 마지막에 시작한다. */
    Xil_ExceptionEnable();
    XTmrCtr_Start(&s_timer, 0U);
    return 0;
}

int platform_init(void)
{

    /* 로봇 PWM enable 여부는 robot_motion_init()가 결정한다. */
    servo_hal_init();

    /* UART 초기화가 실패하면 서보를 켜기 전에 끝낸다(RTL reset 상태라 PWM 출력은 꺼져 있다). */
    if (console_uart_init() != 0) {
        xil_printf("[platform] UART init failed\r\n");
        return -1;
    }
    if (tick_timer_start() != 0) {
        xil_printf("[platform] tick timer init failed\r\n");
        return -1;
    }

    /* [TRACE] 배너에 UART 속도를 함께 찍는다. PC의 baud가 다르면 이 줄이 깨져 보인다. */
    xil_printf("[platform] ready (tick %d ms, uart %u baud, trace on)\r\n",
               (int)PLATFORM_TICK_MS, (unsigned)ROBOT_CONSOLE_BAUD);
    return 0;
}

XScuGic *platform_vitis_gic(void)
{
    return &s_gic;
}






int platform_tick_due(void)
{
    uint32_t count = s_tick_count; /* 한 번만 읽는다. 32비트 정렬 읽기는 원자적이다. */
    uint32_t pending;

    if (!s_tick_armed) {
        /* 루프 시작 전(초기화 중)에 쌓인 틱은 버리고 지금부터 센다. */
        s_tick_seen = count;
        s_tick_overruns = 0U;
        s_tick_armed = 1;
        return 0;
    }

    pending = count - s_tick_seen; /* 부호 없는 뺄셈이라 카운터가 한 바퀴 돌아도 맞다. */
    if (pending == 0U) return 0;

    if (pending > 1U) s_tick_overruns += pending - 1U;
    s_tick_seen = count;
    return 1;
}

uint32_t platform_tick_overrun_count(void)
{
    return s_tick_overruns;
}
uint32_t platform_monotonic_us(void)
{
    XTime now;

    XTime_GetTime(&now);
    /* 글로벌 타이머는 CPU 클럭의 절반(COUNTS_PER_SECOND)으로 센다. 32비트로 잘라 차이만 쓴다. */
    return (uint32_t)(now / (COUNTS_PER_SECOND / 1000000U));
}
