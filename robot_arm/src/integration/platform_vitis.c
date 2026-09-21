/*
 * Vitis(Zynq-7000 PS, standalone) 전용 플랫폼/입력 구현.
 *
 * integration/platform.h 와 integration/input_pose.h 에 선언만 있던 훅 5개를 구현한다.
 *   platform_init, platform_tick_due      : 20ms 제어 틱 (AXI Timer 인터럽트)
 *   input_pose_init/ready/take            : PC UART 입력 (Agent1의 uart_pose_rx_* 재사용)
 *
 * 호스트 빌드에는 포함하지 않는다. 호스트 테스트는 이 훅의 가짜 구현을 스스로 제공하고,
 * 루트 CMake에도 이 파일을 등록하지 않는다(uart_pose_rx_vitis.c 와 같은 취급).
 *
 * 설계 요점
 *  - 인터럽트 서비스 루틴(ISR)은 카운터만 올린다. Agent1/2/3은 ISR에서 절대 실행하지 않는다.
 *  - main이 platform_tick_due()로 카운터를 읽어 한 틱만 소비한다. 처리가 길어져 틱이 여러 개
 *    밀리면 따라잡지 않고 버린다(램프가 몰아서 진행하는 것을 막고, 밀린 횟수만 센다).
 *  - UART 수신은 폴링이다. 한 프레임(36B)은 수신 FIFO(64B)에 들어가므로 main 루프가 자주 돌면 충분하다.
 *  - 로그는 부팅과 오류에만 한 줄씩 남긴다. xil_printf는 %f를 지원하지 않고,
 *    프레임마다 찍으면 그 시간만큼 틱이 늦어진다.
 *  - [TRACE] ROBOT_TRACE를 정의하면 UART를 921600 baud로 열고, 맨 아래의 trace 플랫폼 경계 3개를 구현한다
 *    (integration/trace.h). 프레임/틱 로그는 링버퍼에 쌓았다가 main 루프에서 TX FIFO로 논블로킹 전송한다.
 */

#include "integration/input_pose.h"
#include "integration/platform.h"
#include "integration/trace.h"

#include <stddef.h>
#include <stdint.h>

#include "output_controller/servo_hal.h"
#include "uart_pose/uart_pose_rx_vitis.h"

#include "xil_exception.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xscugic.h"
#include "xstatus.h"
#include "xtime_l.h"
#include "xtmrctr.h"

/* ---- 설정값 (XSA가 만든 xparameters.h 기준) ---- */
#define PLATFORM_UART_DEVICE_ID   XPAR_XUARTPS_0_DEVICE_ID              /* PS UART1 (USB-UART) */
/* [TRACE] trace를 켠 빌드는 921600, 끈 빌드는 115200이다. PC 스크립트도 같은 속도로 열어야 한다(--baud). */
#ifdef ROBOT_TRACE
#define PLATFORM_UART_BAUD        ROBOT_TRACE_UART_BAUD
#else
#define PLATFORM_UART_BAUD        115200U
#endif
#define PLATFORM_GIC_DEVICE_ID    XPAR_SCUGIC_0_DEVICE_ID
#define PLATFORM_TIMER_DEVICE_ID  XPAR_AXI_TIMER_0_DEVICE_ID
#define PLATFORM_TIMER_IRQ_ID     XPAR_FABRIC_AXI_TIMER_0_INTERRUPT_INTR /* IRQ_F2P[0] = 61 */

#define PLATFORM_TICK_MS          20U                                   /* 제어 주기 20ms = 50Hz */
/* 다운 카운트 자동 재적재 값: 100MHz x 20ms = 2,000,000. 실제 주기와의 차이는 수 클럭(수십 ns)이라 무시한다. */
#define PLATFORM_TIMER_RELOAD     ((XPAR_AXI_TIMER_0_CLOCK_FREQ_HZ / 1000U) * PLATFORM_TICK_MS)

#define PLATFORM_IRQ_PRIORITY     0xA0U /* 0(높음)~0xF8(낮음). 쓰는 인터럽트가 이것 하나라 값은 크게 중요하지 않다. */
#define PLATFORM_IRQ_LEVEL_HIGH   0x1U  /* AXI Timer의 interrupt 출력은 레벨 하이다(XSA hwh에서 확인). */

/* 첫 프레임의 dt: 공칭 프레임 주기(20Hz = 0.05초). Agent1 담당자 권고. */
#define INPUT_NOMINAL_DT_SEC      0.05f

static XScuGic s_gic;
static XTmrCtr s_timer;
static UartPoseReceiver s_rx;

/*
 * 틱 상태. s_tick_count는 ISR만 올리고 나머지는 main만 갱신한다.
 * 쓰는 쪽이 하나씩이라 잠금이 필요 없고, main은 카운터를 한 번만 읽어서 쓴다.
 */
static volatile uint32_t s_tick_count;
static uint32_t s_tick_seen;
static uint32_t s_tick_overruns; /* 한 틱보다 더 밀려서 버린 틱 수. 디버거로 확인한다. */
static int s_tick_armed;

/* 입력 상태 */
static XTime s_last_frame_time;
static int s_have_last_frame;

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
    /* PWM enable는 여기서 하지 않는다. 홈 자세를 쓴 뒤 agent_pipeline_init()이 한다. */
    servo_hal_init();

    /* UART 초기화가 실패하면 서보를 켜기 전에 끝낸다(RTL reset 상태라 PWM 출력은 꺼져 있다). */
    if (uart_pose_rx_init(&s_rx, PLATFORM_UART_DEVICE_ID, PLATFORM_UART_BAUD) != XST_SUCCESS) {
        xil_printf("[platform] UART init failed\r\n");
        return -1;
    }
    if (tick_timer_start() != 0) {
        xil_printf("[platform] tick timer init failed\r\n");
        return -1;
    }

#ifdef ROBOT_TRACE
    /* [TRACE] 배너에 UART 속도를 함께 찍는다. PC의 baud가 다르면 이 줄이 깨져 보인다. */
    xil_printf("[platform] ready (tick %d ms, uart %u baud, trace on)\r\n",
               (int)PLATFORM_TICK_MS, (unsigned)PLATFORM_UART_BAUD);
#else
    xil_printf("[platform] ready (tick %d ms)\r\n", (int)PLATFORM_TICK_MS);
#endif
    return 0;
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

void input_pose_init(void)
{
    /* 수신기 자체는 platform_init()에서 초기화했다. 여기서는 프레임 간격 기준만 리셋한다. */
    s_have_last_frame = 0;
}

int input_pose_ready(void)
{
    uart_pose_rx_poll(&s_rx); /* 수신 FIFO를 비우며 파싱한다. */
    return s_rx.pose_ready ? 1 : 0;
}

int input_pose_take(HumanPose2D *pose, float *dt_sec)
{
    XTime now;

    if (pose == NULL || dt_sec == NULL) return 0;
    if (!uart_pose_rx_take_frame(&s_rx, pose)) return 0;

    /* dt는 직전에 넘긴 프레임과의 실제 간격이다. 첫 프레임은 공칭 주기를 쓴다. */
    XTime_GetTime(&now);
    if (s_have_last_frame) {
        *dt_sec = (float)((double)(now - s_last_frame_time) / (double)COUNTS_PER_SECOND);
    } else {
        *dt_sec = INPUT_NOMINAL_DT_SEC;
    }
    s_last_frame_time = now;
    s_have_last_frame = 1;
    return 1;
}

#ifdef ROBOT_TRACE
/*
 * [TRACE] trace 플랫폼 경계 (integration/trace.h). 호스트 테스트는 이 3개의 가짜 구현을 제공한다.
 */

uint32_t platform_trace_time_us(void)
{
    XTime now;

    XTime_GetTime(&now);
    /* 글로벌 타이머는 CPU 클럭의 절반(COUNTS_PER_SECOND)으로 센다. 32비트로 잘라 차이만 쓴다. */
    return (uint32_t)(now / (COUNTS_PER_SECOND / 1000000U));
}

void platform_trace_stats(TracePlatformStats *out)
{
    out->tick_overruns = s_tick_overruns;
    out->uart_crc_errors = s_rx.parser.crc_errors;
    out->uart_format_errors = s_rx.parser.format_errors;
    out->uart_range_errors = s_rx.parser.range_errors;
    out->uart_overwritten = s_rx.overwritten_frames;
}

uint32_t platform_trace_tx(const uint8_t *data, uint32_t len)
{
    UINTPTR base = s_rx.uart.Config.BaseAddress; /* 수신기가 쓰는 UART와 같은 장치 */
    uint32_t sent = 0U;

    /* TX FIFO(64B)에 자리가 있을 때만 넣고 바로 돌아온다. xil_printf와 달리 기다리지 않는다. */
    while (sent < len && !XUartPs_IsTransmitFull(base)) {
        XUartPs_WriteReg(base, XUARTPS_FIFO_OFFSET, data[sent]);
        ++sent;
    }
    return sent;
}
#endif /* ROBOT_TRACE */
