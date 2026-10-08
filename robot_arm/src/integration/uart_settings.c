#include "integration/uart_settings.h"
#include "ff.h"
#include "xil_printf.h"
#include <string.h>

#define SETTINGS_PATH "0:/UARTCFG.BIN"
#define SETTINGS_BACKUP "0:/UARTCFG.BAK"
#define SETTINGS_NEW "0:/UARTCFG.NEW"
#define SETTINGS_BYTES (24U + UART_SETTINGS_FIELDS * 4U)

static unsigned settings_role;
static UartSettingsCapture capture_values;
static UartSettingsApply apply_values;
static unsigned parser_active, parser_bad, parser_length, pending;
static char parser_text[24];

static uint32_t read_word(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void write_word(unsigned char *bytes, uint32_t value)
{
    unsigned index;
    for (index = 0U; index < 4U; ++index) bytes[index] = (unsigned char)(value >> (index * 8U));
}

static uint32_t crc32(const unsigned char *bytes, unsigned length)
{
    uint32_t crc = 0xffffffffU;
    unsigned index, bit;
    for (index = 0U; index < length; ++index) {
        crc ^= bytes[index];
        for (bit = 0U; bit < 8U; ++bit) crc = (crc >> 1) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
    return ~crc;
}

int uart_settings_valid(const UartSettingsValues *values, unsigned role)
{
    unsigned index;
    if (!values || (role != 1U && role != 2U)) return 0;
    for (index = 0U; index < 11U; ++index) if (values->value[index] > 255U) return 0;
    if (!values->value[11] || values->value[11] > 262143U || values->value[12] > 15U) return 0;
    if (role == 1U) {
        for (index = 13U; index < UART_SETTINGS_FIELDS; ++index) if (values->value[index]) return 0;
    } else {
        if (values->value[13] < 1000U || values->value[13] > 1000000U) return 0;
        for (index = 14U; index < 16U; ++index)
            if (values->value[index] < 100U || values->value[index] > 1000000U) return 0;
        if (values->value[16] > 5000000U || values->value[17] > 5000000U) return 0;
    }
    return 1;
}

static int load_file(const char *path, UartSettingsValues *values)
{
    FIL file;
    unsigned char bytes[SETTINGS_BYTES];
    UINT received = 0U;
    unsigned index;
    FRESULT result;
    if (f_open(&file, path, FA_READ) != FR_OK) return 0;
    result = f_read(&file, bytes, sizeof(bytes), &received);
    if (result != FR_OK || received != sizeof(bytes) || f_size(&file) != sizeof(bytes)) {
        (void)f_close(&file); return 0;
    }
    if (f_close(&file) != FR_OK || memcmp(bytes, "UCF1", 4U) || read_word(bytes + 4) != 1U ||
        read_word(bytes + 8) != 0x77D4E3BBU || read_word(bytes + 12) != settings_role ||
        read_word(bytes + 16) != UART_SETTINGS_FIELDS ||
        read_word(bytes + 20) != crc32(bytes + 24, UART_SETTINGS_FIELDS * 4U)) return 0;
    for (index = 0U; index < UART_SETTINGS_FIELDS; ++index)
        values->value[index] = read_word(bytes + 24U + index * 4U);
    return uart_settings_valid(values, settings_role);
}

static int restore(void)
{
    UartSettingsValues values, previous;
    const char *source = SETTINGS_PATH;
    if (!load_file(source, &values)) {
        source = SETTINGS_BACKUP;
        if (!load_file(source, &values)) {
            xil_printf("[CFG] LOAD_FAILED missing_invalid_role_ABI_CRC; current settings unchanged\r\n");
            return 0;
        }
    }
    capture_values(&previous);
    if (!apply_values(&values)) {
        int rollback = apply_values(&previous);
        xil_printf("[CFG] APPLY_FAILED rollback=%u; no automatic PWM enable\r\n", (unsigned)rollback);
        return 0;
    }
    xil_printf("[CFG] LOADED source=%s role=%u; PWM/live state unchanged\r\n", source, settings_role);
    return 1;
}

static int save(void)
{
    UartSettingsValues values, checked, previous;
    unsigned char bytes[SETTINGS_BYTES];
    FIL file;
    FILINFO info;
    FRESULT result;
    UINT written = 0U;
    unsigned index;
    int had_original;
    capture_values(&values);
    if (!uart_settings_valid(&values, settings_role)) return 0;
    memcpy(bytes, "UCF1", 4U);
    write_word(bytes + 4, 1U);
    write_word(bytes + 8, 0x77D4E3BBU);
    write_word(bytes + 12, settings_role);
    write_word(bytes + 16, UART_SETTINGS_FIELDS);
    for (index = 0U; index < UART_SETTINGS_FIELDS; ++index)
        write_word(bytes + 24U + index * 4U, values.value[index]);
    write_word(bytes + 20, crc32(bytes + 24, UART_SETTINGS_FIELDS * 4U));
    if (f_stat(SETTINGS_NEW, &info) == FR_OK && f_unlink(SETTINGS_NEW) != FR_OK) return 0;
    if (f_open(&file, SETTINGS_NEW, FA_WRITE | FA_CREATE_NEW) != FR_OK) return 0;
    result = f_write(&file, bytes, sizeof(bytes), &written);
    if (result == FR_OK && written == sizeof(bytes)) result = f_sync(&file);
    else result = FR_DISK_ERR;
    if (f_close(&file) != FR_OK) result = FR_DISK_ERR;
    if (result != FR_OK || !load_file(SETTINGS_NEW, &checked) || memcmp(&values, &checked, sizeof(values))) {
        (void)f_unlink(SETTINGS_NEW); return 0;
    }
    had_original = f_stat(SETTINGS_PATH, &info) == FR_OK;
    if (had_original && !load_file(SETTINGS_PATH, &previous)) {
        if (f_unlink(SETTINGS_PATH) != FR_OK) return 0;
        had_original = 0;
    }
    if (had_original) {
        if (f_stat(SETTINGS_BACKUP, &info) == FR_OK && f_unlink(SETTINGS_BACKUP) != FR_OK) return 0;
        if (f_rename(SETTINGS_PATH, SETTINGS_BACKUP) != FR_OK) return 0;
    }
    if (f_rename(SETTINGS_NEW, SETTINGS_PATH) != FR_OK) {
        if (had_original) (void)f_rename(SETTINGS_BACKUP, SETTINGS_PATH);
        return 0;
    }
    return load_file(SETTINGS_PATH, &checked) && !memcmp(&values, &checked, sizeof(values));
}

static void report(void)
{
    UartSettingsValues values;
    capture_values(&values);
    xil_printf("[CFG] role=%u red=%u green=%u blue=%u yellow_delta=%u yellow_gap=%u\r\n",
        settings_role, (unsigned)values.value[0], (unsigned)values.value[1], (unsigned)values.value[2],
        (unsigned)values.value[3], (unsigned)values.value[4]);
    xil_printf("[CFG] brightness R/G/B=%u/%u/%u yellow R/G/Bmax=%u/%u/%u min_count=%u mask=%u\r\n",
        (unsigned)values.value[5], (unsigned)values.value[6], (unsigned)values.value[7],
        (unsigned)values.value[8], (unsigned)values.value[9], (unsigned)values.value[10],
        (unsigned)values.value[11], (unsigned)values.value[12]);
    if (settings_role == 2U)
        xil_printf("[CFG] backend=G ema_tau_us=%u kxy_milli_mm=%u kz_milli_mm=%u kacc_milli_mm_s2=%u kvel_milli_mm_s=%u\r\n",
            (unsigned)values.value[13], (unsigned)values.value[14], (unsigned)values.value[15],
            (unsigned)values.value[16], (unsigned)values.value[17]);
    xil_printf("[CFG] persistence=SD path=%s; save/load require PWM_OFF and CNN_OFF; pose/PWM state not saved\r\n", SETTINGS_PATH);
}

void uart_settings_init(unsigned role, UartSettingsCapture capture, UartSettingsApply apply)
{
    settings_role = role;
    capture_values = capture;
    apply_values = apply;
    parser_active = parser_bad = parser_length = pending = 0U;
    uart_settings_report_capability();
}

void uart_settings_report_capability(void)
{
    xil_printf("[CFG] supported=1 role=%u protocol=@CFG schema=1; show/save/load\r\n", settings_role);
}

int uart_settings_active(void) { return (int)parser_active; }

void uart_settings_abort_input(void)
{
    parser_active = parser_bad = parser_length = pending = 0U;
}

int uart_settings_feed(uint8_t byte, int blocked)
{
    if (!parser_active) {
        if (byte != '@') return 0;
        parser_active = 1U; parser_bad = (unsigned)blocked; parser_length = 0U;
        return 1;
    }
    if (byte == '\r') {
        parser_text[parser_length] = '\0';
        if (parser_bad || pending) xil_printf("[CFG] REJECTED format_menu_or_pending\r\n");
        else if (!strcmp(parser_text, "CFG,SHOW")) pending = 1U;
        else if (!strcmp(parser_text, "CFG,SAVE")) pending = 2U;
        else if (!strcmp(parser_text, "CFG,LOAD")) pending = 3U;
        else xil_printf("[CFG] REJECTED unknown_command\r\n");
        parser_active = parser_bad = parser_length = 0U;
    } else if (byte < 32U || byte > 126U || parser_length >= sizeof(parser_text) - 1U) parser_bad = 1U;
    else if (!parser_bad) parser_text[parser_length++] = (char)byte;
    return 1;
}

int uart_settings_service(int storage_safe)
{
    unsigned action = pending;
    if (!action) return 0;
    pending = 0U;
    if (action == 1U) { report(); return 0; }
    if (!storage_safe) {
        xil_printf("[CFG] REJECTED require PWM_OFF CNN_OFF and no active menu/record/replay\r\n");
        return 0;
    }
    if (action == 2U) xil_printf("[CFG] %s path=%s\r\n", save() ? "SAVED" : "SAVE_FAILED", SETTINGS_PATH);
    else (void)restore();
    report();
    return 1;
}

void uart_settings_boot_restore(void)
{
    if (restore()) report();
    else xil_printf("[CFG] boot defaults retained unless APPLY_FAILED; PWM remains OFF\r\n");
}
