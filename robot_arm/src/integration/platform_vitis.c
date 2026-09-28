/*
 * Vitis(Zynq-7000 PS, standalone) 전용 플랫폼/입력 구현.
 *
 * platform_init/platform_tick_due: 20ms 제어 틱 (AXI Timer 인터럽트).
 * CNN 인터럽트는 공유 GIC를 통해 cnn_bringup이 등록한다.
 * input_pose_* 구현은 input_pose_cnn.c에 있다.
 *
 * 호스트 빌드에는 포함하지 않는다. 호스트 테스트는 이 훅의 가짜 구현을 스스로 제공하고,
 * 루트 CMake에도 이 파일을 등록하지 않는다(uart_pose_rx_vitis.c 와 같은 취급).
 *
 * 설계 요점
 *  - 인터럽트 서비스 루틴(ISR)은 카운터만 올린다. Agent1/2/3은 ISR에서 절대 실행하지 않는다.
 *  - main이 platform_tick_due()로 카운터를 읽어 한 틱만 소비한다. 처리가 길어져 틱이 여러 개
 *    밀리면 따라잡지 않고 버린다(램프가 몰아서 진행하는 것을 막고, 밀린 횟수만 센다).
 *  - UART는 CNN 설정 명령과 TRACE 출력에 사용한다. 자세 입력은 CNN 결과에서 온다.
 *  - xil_printf와 TRACE는 공유 TX 링을 통해 메인 루프에서 전송한다.
 *  - [TRACE] ROBOT_TRACE를 정의하면 UART를 921600 baud로 열고, 맨 아래의 trace 플랫폼 경계 3개를 구현한다
 *    (integration/trace.h). 프레임/틱 로그는 링버퍼에 쌓았다가 main 루프에서 TX FIFO로 논블로킹 전송한다.
 */

#include "integration/platform.h"
#include "integration/platform_vitis.h"
#include "integration/trace.h"

#include <stddef.h>
#include <stdint.h>

#include "output_controller/servo_hal.h"

#include "xil_exception.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xscugic.h"
#include "xstatus.h"
#include "xtime_l.h"
#include "xtmrctr.h"
#include "xuartps.h"
#include "xuartps_hw.h"

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
#define PLATFORM_TIMER_IRQ_ID     XPAR_FABRIC_AXI_TIMER_0_INTERRUPT_INTR /* CNN XSA: IRQ 62 */

#define PLATFORM_TICK_MS          20U                                   /* 제어 주기 20ms = 50Hz */
/* 다운 카운트 자동 재적재 값: 100MHz x 20ms = 2,000,000. 실제 주기와의 차이는 수 클럭(수십 ns)이라 무시한다. */
#define PLATFORM_TIMER_RELOAD     ((XPAR_AXI_TIMER_0_CLOCK_FREQ_HZ / 1000U) * PLATFORM_TICK_MS)

#define PLATFORM_IRQ_PRIORITY     0xA0U /* CNN IRQ와 같은 우선순위. */
#define PLATFORM_IRQ_LEVEL_HIGH   0x1U  /* AXI Timer의 interrupt 출력은 레벨 하이다(XSA hwh에서 확인). */

static XScuGic s_gic;
static XTmrCtr s_timer;
static XUartPs s_uart;
#define PLATFORM_TX_RING_SIZE 32768U
static uint8_t s_uart_tx_ring[PLATFORM_TX_RING_SIZE];
static uint32_t s_uart_tx_head;
static uint32_t s_uart_tx_tail;
static int s_uart_ready;
static int s_uart_output_enabled = 1;

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
    XUartPs_Config *uart_cfg;

    /* 로봇 PWM enable 여부는 agent_pipeline_init_mode()가 결정한다. */
    servo_hal_init();

    /* UART 초기화가 실패하면 서보를 켜기 전에 끝낸다(RTL reset 상태라 PWM 출력은 꺼져 있다). */
    uart_cfg = XUartPs_LookupConfig(PLATFORM_UART_DEVICE_ID);
    if (uart_cfg == NULL ||
        XUartPs_CfgInitialize(&s_uart, uart_cfg, uart_cfg->BaseAddress) != XST_SUCCESS ||
        XUartPs_SetBaudRate(&s_uart, PLATFORM_UART_BAUD) != XST_SUCCESS) {
        xil_printf("[platform] UART init failed\r\n");
        return -1;
    }
    s_uart_tx_head = 0U;
    s_uart_tx_tail = 0U;
    s_uart_ready = 1;
    s_uart_output_enabled = 1;
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

XScuGic *platform_vitis_gic(void)
{
    return &s_gic;
}

void platform_uart_service(void)
{
    UINTPTR base;
    if (!s_uart_ready || !s_uart_output_enabled) return;
    base = s_uart.Config.BaseAddress;
    while (s_uart_tx_head != s_uart_tx_tail && !XUartPs_IsTransmitFull(base)) {
        XUartPs_WriteReg(base, XUARTPS_FIFO_OFFSET,
                        s_uart_tx_ring[s_uart_tx_tail & (PLATFORM_TX_RING_SIZE - 1U)]);
        ++s_uart_tx_tail;
    }
}

void platform_uart_set_output_enabled(int enabled)
{
    s_uart_output_enabled = enabled ? 1 : 0;
    if (!s_uart_output_enabled) {
        /* Drop queued text and TRACE bytes; hardware FIFO may finish sending
         * a few bytes already accepted before the mute command. */
        s_uart_tx_tail = s_uart_tx_head;
    }
}

int platform_uart_output_enabled(void)
{
    return s_uart_output_enabled;
}

/* Xilinx xil_printf calls outbyte. Keep its output and ROBOT_TRACE ordered. */
void outbyte(char c)
{
    if (!s_uart_output_enabled) return;
    if (!s_uart_ready) {
        XUartPs_SendByte(STDOUT_BASEADDRESS, (u8)c);
        return;
    }
    while (s_uart_tx_head - s_uart_tx_tail == PLATFORM_TX_RING_SIZE)
        platform_uart_service();
    s_uart_tx_ring[s_uart_tx_head & (PLATFORM_TX_RING_SIZE - 1U)] = (uint8_t)c;
    ++s_uart_tx_head;
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
    out->uart_crc_errors = 0U;
    out->uart_format_errors = 0U;
    out->uart_range_errors = 0U;
    out->uart_overwritten = 0U;
}

uint32_t platform_trace_tx(const uint8_t *data, uint32_t len)
{
    uint32_t i;
    if (data == NULL) return 0U;
    if (!s_uart_output_enabled) return len;
    if (len > PLATFORM_TX_RING_SIZE -
                               (s_uart_tx_head - s_uart_tx_tail)) return 0U;
    for (i = 0U; i < len; ++i)
        s_uart_tx_ring[(s_uart_tx_head + i) & (PLATFORM_TX_RING_SIZE - 1U)] = data[i];
    s_uart_tx_head += len;
    return len;
}
#endif /* ROBOT_TRACE */
