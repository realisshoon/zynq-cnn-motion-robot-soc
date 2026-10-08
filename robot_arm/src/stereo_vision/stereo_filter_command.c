#include "stereo_vision/stereo_filter_command.h"

#include <stddef.h>
#include <string.h>

static int parse_value(const char *text, uint32_t *value)
{
    uint32_t parsed = 0U;
    unsigned digits = 0U;
    while (*text) {
        if (*text < '0' || *text > '9' || ++digits > 7U) return 0;
        parsed = parsed * 10U + (unsigned)(*text++ - '0');
    }
    if (!digits) return 0;
    *value = parsed;
    return 1;
}

static int parse_command(const char *text, StereoFilterCommand *command)
{
    static const struct {
        const char *prefix;
        StereoFilterAction action;
        uint32_t low, high;
    } fields[] = {
        {"F,EMA,", STEREO_FILTER_EMA, 1000U, 1000000U},
        {"F,MIN,", STEREO_FILTER_MIN, 10U, 10000U},
        {"F,BETA,", STEREO_FILTER_BETA, 0U, 100000U},
        {"F,DERIVATIVE,", STEREO_FILTER_DERIVATIVE, 10U, 10000U},
        {"F,KXY,", STEREO_FILTER_KALMAN_XY, 100U, 1000000U},
        {"F,KZ,", STEREO_FILTER_KALMAN_Z, 100U, 1000000U},
        {"F,KACC,", STEREO_FILTER_KALMAN_ACCEL, 0U, 5000000U},
        {"F,KVEL,", STEREO_FILTER_KALMAN_VELOCITY, 0U, 5000000U}
    };
    unsigned index;
    if (strcmp(text, "F,SHOW") == 0 || strcmp(text, "F,DEFAULT") == 0) {
        command->action = strcmp(text, "F,SHOW") == 0
            ? STEREO_FILTER_SHOW : STEREO_FILTER_DEFAULT;
        command->value = 0U;
        return 1;
    }
    for (index = 0U; index < sizeof(fields) / sizeof(fields[0]); ++index) {
        size_t length = strlen(fields[index].prefix);
        uint32_t value;
        if (strncmp(text, fields[index].prefix, length) != 0) continue;
        if (!parse_value(text + length, &value) || value < fields[index].low ||
            value > fields[index].high) return 0;
        command->action = fields[index].action;
        command->value = value;
        return 1;
    }
    return 0;
}

StereoFilterCommandStatus stereo_filter_command_feed(StereoFilterCommandParser *parser,
    uint8_t byte, int menu_active, StereoFilterCommand *command)
{
    StereoFilterCommandStatus status;
    if (parser == NULL || command == NULL) return STEREO_FILTER_NOT_HANDLED;
    if (parser->skip_lf) {
        parser->skip_lf = 0U;
        if (byte == '\n') return STEREO_FILTER_BUFFERED;
    }
    if (byte == '~') {
        parser->blocked = (uint8_t)(menu_active || (parser->active && parser->blocked));
        parser->active = 1U;
        parser->length = 0U;
        parser->invalid = 0U;
        return STEREO_FILTER_BUFFERED;
    }
    if (!parser->active) return STEREO_FILTER_NOT_HANDLED;
    if (byte == '\r') {
        parser->text[parser->length] = '\0';
        status = parser->blocked ? STEREO_FILTER_MENU_ACTIVE :
            parser->invalid || !parse_command(parser->text, command)
                ? STEREO_FILTER_MALFORMED : STEREO_FILTER_COMPLETE;
        memset(parser, 0, sizeof(*parser));
        parser->skip_lf = byte == '\r';
        return status;
    }
    if (byte < 32U || byte > 126U || parser->length == sizeof(parser->text) - 1U)
        parser->invalid = 1U;
    if (!parser->invalid) parser->text[parser->length++] = (char)byte;
    return STEREO_FILTER_BUFFERED;
}
