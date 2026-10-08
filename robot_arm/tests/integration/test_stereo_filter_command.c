#include "stereo_vision/stereo_filter_command.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static StereoFilterCommandStatus feed(const char *text, int blocked,
    StereoFilterCommand *command)
{
    StereoFilterCommandParser parser = {0};
    StereoFilterCommandStatus status = STEREO_FILTER_NOT_HANDLED;
    while (*text) status = stereo_filter_command_feed(&parser, (uint8_t)*text++, blocked, command);
    return status;
}

int main(void)
{
    StereoFilterCommand command;
    StereoFilterCommandParser parser = {0};
    unsigned index;
    const char *invalid[] = {"~F,MIN,0\r", "~F,MIN,10001\r", "~F,BETA,-1\r",
        "~F,BETA,100001\r", "~F,EMA,999\r", "~F,EMA,1000001\r",
        "~F,EMA,99999999\r", "~F,MIN,500E\r", "~F,MIN,0.5\r",
        "~F,MIN,500,\r", "~F,SHOW,1\r", "~EAPX\r", "~F,MIN,NaN\r",
        "~F,MIN,500\t\r", "~\r"};
    assert(feed("~F,SHOW\r", 0, &command) == STEREO_FILTER_COMPLETE);
    assert(command.action == STEREO_FILTER_SHOW);
    assert(feed("~F,DEFAULT\r", 0, &command) == STEREO_FILTER_COMPLETE);
    assert(command.action == STEREO_FILTER_DEFAULT);
    assert(feed("~F,EMA,150000\r", 0, &command) == STEREO_FILTER_COMPLETE);
    assert(command.action == STEREO_FILTER_EMA && command.value == 150000U);
    assert(feed("~F,MIN,500\r", 0, &command) == STEREO_FILTER_COMPLETE);
    assert(command.action == STEREO_FILTER_MIN && command.value == 500U);
    assert(feed("~F,BETA,0\r", 0, &command) == STEREO_FILTER_COMPLETE);
    assert(command.action == STEREO_FILTER_BETA && command.value == 0U);
    assert(feed("~F,DERIVATIVE,1000\r", 0, &command) == STEREO_FILTER_COMPLETE);
    assert(command.action == STEREO_FILTER_DERIVATIVE && command.value == 1000U);
    assert(feed("~F,bad\nE\r", 0, &command) == STEREO_FILTER_MALFORMED);
    assert(feed("~F,SHOW\nE\r", 0, &command) == STEREO_FILTER_MALFORMED);
    for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index)
        assert(feed(invalid[index], 0, &command) == STEREO_FILTER_MALFORMED);
    assert(feed("~F,EMA,150000\r", 1, &command) == STEREO_FILTER_MENU_ACTIVE);
    assert(stereo_filter_command_feed(&parser, 'E', 0, &command) == STEREO_FILTER_NOT_HANDLED);
    assert(stereo_filter_command_feed(&parser, '~', 0, &command) == STEREO_FILTER_BUFFERED);
    for (index = 0U; index < 100U; ++index)
        assert(stereo_filter_command_feed(&parser, 'E', 0, &command) == STEREO_FILTER_BUFFERED);
    assert(stereo_filter_command_feed(&parser, '\r', 0, &command) == STEREO_FILTER_MALFORMED);
    assert(stereo_filter_command_feed(&parser, '\n', 1, &command) == STEREO_FILTER_BUFFERED);
    assert(stereo_filter_command_feed(&parser, 'E', 0, &command) == STEREO_FILTER_NOT_HANDLED);
    assert(feed("~EAP~F,MIN,500\r", 0, &command) == STEREO_FILTER_COMPLETE);
    puts("test_stereo_filter_command: PASS (bounded frames, menu isolation, CRLF and no payload dispatch)");
    return 0;
}
