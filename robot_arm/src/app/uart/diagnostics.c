#include "app/uart/telemetry.h"
#include "drivers/system/platform.h"
#include "drivers/uart/console_uart.h"

uint32_t platform_telemetry_time_us(void) { return platform_monotonic_us(); }

void platform_telemetry_stats(TelemetryPlatformStats *out)
{
    out->tick_overruns = platform_tick_overrun_count();
    out->uart_crc_errors = 0U;
    out->uart_format_errors = 0U;
    out->uart_range_errors = 0U;
    out->uart_overwritten = 0U;
}

uint32_t platform_telemetry_tx(const uint8_t *data, uint32_t len)
{
    return console_uart_enqueue(data, len);
}
