#ifndef INTEGRATION_TRACE_H
#define INTEGRATION_TRACE_H

/*
 * UART 디버그 로그(trace).
 *
 * 파이프라인 각 단계(Agent1/2/3)의 입출력을 PC로 한 줄씩 보낸다.
 * main_integration.c의 TRACE_* 호출 지점에서 실행되며, ROBOT_TRACE를 정의한 빌드에서만 켜진다.
 * 정의하지 않으면 TRACE_*는 아무 코드도 만들지 않는다. 변경 지점은 "[TRACE]" 태그로 찾을 수 있다.
 *
 * 설계 요점
 *  - 로그 줄은 링버퍼에 쌓기만 한다(블로킹 없음). 버퍼가 차면 그 줄을 통째로 버리고 drop을 센다.
 *  - 링버퍼는 main 루프가 한 바퀴 돌 때마다 UART TX FIFO에 들어가는 만큼만 비우고 돌아온다.
 *  - ISR에서는 부르지 않는다.
 *  - trace를 켠 빌드는 UART를 921600 baud로 쓴다(끈 빌드는 115200). PC 스크립트도 --baud 921600으로 연다.
 *  - 줄 형식과 사용법은 README의 "UART 로그" 절을 본다.
 */

#ifdef ROBOT_TRACE

#include <stdint.h>

#include "integration/agent_pipeline.h"

/* trace를 켠 빌드의 UART 속도. 끈 빌드는 115200을 유지한다(PC 스크립트의 기본값과 같다). */
#define ROBOT_TRACE_UART_BAUD 921600U

/* 플랫폼이 세는 누적 통계. SM 줄에 실린다. */
typedef struct {
    uint32_t tick_overruns;       /* 한 틱보다 더 밀려서 버린 틱 수 */
    uint32_t uart_crc_errors;     /* PC->보드 UART 패킷 CRC 오류 */
    uint32_t uart_format_errors;  /* PC->보드 UART 패킷 형식 오류 */
    uint32_t uart_range_errors;   /* PC->보드 UART 패킷 좌표 범위 오류 */
    uint32_t uart_overwritten;    /* main이 읽기 전에 덮어쓴 프레임 수 */
} TracePlatformStats;

/*
 * ---- 플랫폼 경계: 구현은 platform_vitis.c(보드) 또는 테스트 코드(호스트) ----
 */

/* 부팅 후 시간(마이크로초). 32비트라 71분마다 한 바퀴 돈다. 차이만 쓰므로 한 바퀴 돌아도 맞다. */
uint32_t platform_trace_time_us(void);

/* 누적 통계를 채운다. */
void platform_trace_stats(TracePlatformStats *out);

/* TX FIFO에 지금 들어가는 만큼만 넣고 넣은 바이트 수를 돌려준다. 기다리지 않는다. */
uint32_t platform_trace_tx(const uint8_t *data, uint32_t len);

/*
 * ---- trace 본체 (trace.c) ----
 */

/* 부팅 직후 한 번: 컬럼 정의(# 줄)와 BOOT 이벤트를 쌓는다. platform_init() 뒤에 부른다. */
void trace_init(void);

/* 실행시간(dur_us) 측정 시작 시각을 지금으로 잡는다. */
void trace_mark(void);

/* agent1_run 직후. A1 줄과 P3 줄(Agent1 내부 Point3D)을 쌓는다. agent2_run 전에 불러야 원본 타겟이 보인다. */
void trace_a1(const AgentPipelineContext *ctx);

/* agent2_run 직후. A2 줄(결과, 거부 사유, 보정된 명령)을 쌓는다. */
void trace_a2(const AgentPipelineContext *ctx);

/* agent3_run 직후(제어 틱 1회 뒤). TK 줄을 쌓는다. */
void trace_tick(const AgentPipelineContext *ctx);

/* main 루프 한 바퀴의 맨 끝: 링버퍼를 UART로 비우고, 1초마다 SM 줄을 쌓는다. */
void trace_poll(const AgentPipelineContext *ctx);

#define TRACE_INIT()   trace_init()
#define TRACE_MARK()   trace_mark()
#define TRACE_A1(p)    trace_a1(p)
#define TRACE_A2(p)    trace_a2(p)
#define TRACE_TK(p)    trace_tick(p)
#define TRACE_POLL(p)  trace_poll(p)

#else /* !ROBOT_TRACE */

#define TRACE_INIT()   ((void)0)
#define TRACE_MARK()   ((void)0)
#define TRACE_A1(p)    ((void)0)
#define TRACE_A2(p)    ((void)0)
#define TRACE_TK(p)    ((void)0)
#define TRACE_POLL(p)  ((void)0)

#endif /* ROBOT_TRACE */

#endif /* INTEGRATION_TRACE_H */
