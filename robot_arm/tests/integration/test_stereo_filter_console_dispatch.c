#include "stereo_vision/stereo_filter_command.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t values[3];
    uint32_t pending;
    unsigned field, digits;
    int active, skip_lf;
} StubMenu;

typedef struct {
    StereoFilterCommandParser parser;
    StubMenu menu;
    StereoFilterCommand last_command;
    unsigned applied, shown, rejected, menu_rejected, menu_commits;
    unsigned legacy[256];
} StubDispatch;

static void menu_feed(StubDispatch *state, uint8_t byte)
{
    StubMenu *menu = &state->menu;
    if (menu->skip_lf) {
        menu->skip_lf = 0;
        if (byte == '\n') return;
    }
    if (byte == '\r' || byte == '\n') {
        if (menu->digits) menu->values[menu->field] = menu->pending;
        menu->pending = 0U;
        menu->digits = 0U;
        menu->skip_lf = byte == '\r';
        if (++menu->field == 3U) {
            menu->active = 0;
            ++state->menu_commits;
        }
    } else if (byte >= '0' && byte <= '9' && menu->digits < 6U) {
        menu->pending = menu->pending * 10U + (unsigned)(byte - '0');
        ++menu->digits;
    }
}

static void dispatch_byte(StubDispatch *state, uint8_t byte)
{
    StereoFilterCommand command = {0};
    StereoFilterCommandStatus status = stereo_filter_command_feed(
        &state->parser, byte, state->menu.active, &command);
    if (status != STEREO_FILTER_NOT_HANDLED) {
        if (status == STEREO_FILTER_COMPLETE) {
            state->last_command = command;
            if (command.action == STEREO_FILTER_SHOW) ++state->shown;
            else ++state->applied;
        } else if (status == STEREO_FILTER_MENU_ACTIVE) {
            ++state->menu_rejected;
        } else if (status == STEREO_FILTER_MALFORMED) {
            ++state->rejected;
        }
        return;
    }
    if (state->menu.active) {
        menu_feed(state, byte);
        return;
    }
    if (byte == 'm') {
        memset(&state->menu, 0, sizeof(state->menu));
        state->menu.active = 1;
    }
    if (strchr("EXVASTRPCDLmJjafuwgsx", byte) != NULL && byte != 0U)
        ++state->legacy[byte];
}

static void dispatch_text(StubDispatch *state, const char *text)
{
    while (*text) dispatch_byte(state, (uint8_t)*text++);
}

static void assert_no_legacy(const StubDispatch *state)
{
    unsigned index;
    for (index = 0U; index < 256U; ++index) assert(state->legacy[index] == 0U);
}

static void test_fragmented_commands_and_explicit_legacy(void)
{
    StubDispatch state = {0};
    dispatch_text(&state, "~");
    dispatch_text(&state, "F,");
    dispatch_text(&state, "DE");
    dispatch_text(&state, "FAULT");
    assert(state.parser.active && !state.applied && !state.shown);
    assert_no_legacy(&state);
    dispatch_byte(&state, '\r');
    assert(state.applied == 1U && !state.parser.active);
    assert(state.last_command.action == STEREO_FILTER_DEFAULT);
    dispatch_byte(&state, '\n');
    assert_no_legacy(&state);
    dispatch_text(&state, "~F,MIN,10\r~F,BETA,0\r~F,DERIVATIVE,10000\r");
    assert(state.applied == 4U && !state.rejected);
    assert(state.last_command.action == STEREO_FILTER_DERIVATIVE);
    assert(state.last_command.value == 10000U);
    dispatch_text(&state, "~F,SHOW\r\n");
    assert(state.shown == 1U && state.applied == 4U);
    assert_no_legacy(&state);
    dispatch_byte(&state, 'E');
    assert(state.legacy['E'] == 1U && !state.menu.active);
}

static void test_rejected_payloads_never_dispatch(void)
{
    const char *frames[] = {
        "~EAPXSmjDCL\r", "~F,DEFAULT,EAPXSmjDCL\r",
        "~F,EMA,100000EAPXSmjDCL\r", "~F,MIN,4294967295EAPXSmjDCL\r",
        "~F,BETA,-1EAPXSmjDCL\r", "~F,DERIVATIVE,10001EAPXSmjDCL\r",
        "~F,MIN,500\tEAPXSmjDCL\r", "~F,MIN,500\001EAPXSmjDCL\r"
    };
    unsigned index;
    for (index = 0U; index < sizeof(frames) / sizeof(frames[0]); ++index) {
        StubDispatch state = {0};
        dispatch_text(&state, frames[index]);
        assert(state.rejected == 1U && !state.applied && !state.shown);
        assert(!state.parser.active && !state.menu.active && !state.menu_commits);
        assert_no_legacy(&state);
    }
}

static void test_incomplete_and_oversize_frames(void)
{
    StubDispatch state = {0};
    unsigned index;
    dispatch_text(&state, "~F,EMA,");
    dispatch_text(&state, "100000EAPXSmjDCL");
    assert(state.parser.active && !state.applied && !state.rejected);
    assert_no_legacy(&state);
    dispatch_byte(&state, '\r');
    assert(state.rejected == 1U && !state.menu.active);
    memset(&state, 0, sizeof(state));
    dispatch_text(&state, "~F,");
    for (index = 0U; index < 100U; ++index) dispatch_byte(&state, 'E');
    dispatch_text(&state, "APXSmjDCL");
    assert(state.parser.active && state.parser.invalid);
    assert_no_legacy(&state);
    dispatch_byte(&state, '\r');
    dispatch_byte(&state, '\n');
    assert(state.rejected == 1U && !state.applied && !state.menu.active);
    assert_no_legacy(&state);
    dispatch_byte(&state, 'E');
    assert(state.legacy['E'] == 1U);
}

static void test_embedded_lf_retains_frame_until_cr(void)
{
    const char *prefixes[] = {"~F,bad", "~F,SHOW", "~F,EMA,1000"};
    unsigned index;
    for (index = 0U; index < sizeof(prefixes) / sizeof(prefixes[0]); ++index) {
        StubDispatch state = {0};
        dispatch_text(&state, prefixes[index]);
        dispatch_byte(&state, '\n');
        dispatch_text(&state, "EAPXSmjDCL");
        assert_no_legacy(&state);
        assert(state.parser.active && !state.applied && !state.shown);
        assert(!state.menu.active && !state.menu_commits);
        dispatch_byte(&state, '\r');
        assert(state.rejected == 1U && !state.parser.active);
        dispatch_byte(&state, '\n');
        assert_no_legacy(&state);
    }
    {
        StubDispatch state = {0};
        dispatch_text(&state, "~F,");
        for (index = 0U; index < 100U; ++index) dispatch_byte(&state, 'E');
        dispatch_text(&state, "\nEAPXSmjDCL");
        assert_no_legacy(&state);
        assert(state.parser.active && !state.menu.active);
        dispatch_byte(&state, '\r');
        assert(state.rejected == 1U);
    }
}

static void test_partial_numeric_menu_and_crlf(void)
{
    const char *frames[] = {
        "~F,SHOW\r\n", "~F,DEFAULT\r\n", "~F,EMA,100000\r\n",
        "~EAPXSmjDCL\r\n", "~F,MIN,500\nEAPXSmjDCL\r\n"
    };
    unsigned index;
    for (index = 0U; index < sizeof(frames) / sizeof(frames[0]); ++index) {
        StubDispatch state = {0};
        StubMenu before;
        dispatch_text(&state, "m13");
        before = state.menu;
        dispatch_text(&state, frames[index]);
        assert(memcmp(&before, &state.menu, sizeof(before)) == 0);
        assert(state.menu_rejected == 1U && !state.applied && !state.shown);
        assert(state.legacy['m'] == 1U && !state.legacy['E']);
        dispatch_text(&state, "7\r14\r15\r");
        assert(state.menu_commits == 1U && !state.menu.active);
        assert(state.menu.values[0] == 137U);
        assert(state.menu.values[1] == 14U && state.menu.values[2] == 15U);
    }
    {
        StubDispatch state = {0};
        dispatch_text(&state, "m13\r~F,SHOW\r\n14\r15\r");
        assert(state.menu_commits == 1U && state.menu_rejected == 1U);
        assert(state.menu.values[0] == 13U);
        assert(state.menu.values[1] == 14U && state.menu.values[2] == 15U);
    }
}

int main(void)
{
    test_fragmented_commands_and_explicit_legacy();
    test_rejected_payloads_never_dispatch();
    test_incomplete_and_oversize_frames();
    test_embedded_lf_retains_frame_until_cr();
    test_partial_numeric_menu_and_crlf();
    puts("test_stereo_filter_console_dispatch: PASS (parser with simulated menu and legacy routing)");
    return 0;
}
