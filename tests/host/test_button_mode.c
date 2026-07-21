#include <stdio.h>
#include <stdlib.h>

#include "mode_transition.h"

static int s_pass = 0;
static int s_fail = 0;

#define TEST(name) do { printf("  %-50s ", name); } while (0)
#define PASS()     do { printf("PASS\n"); s_pass++; } while (0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); s_fail++; } while (0)

// ── T01: NORMAL → QUIET ──
static void test_normal_to_quiet(void)
{
    TEST("T01: NORMAL short press -> QUIET");
    mode_transition_result_t r = mode_transition_short_press(MODE_NORMAL, false, MODE_NORMAL);
    if (r.user_mode != MODE_QUIET)  { FAIL("mode should be QUIET"); return; }
    if (!r.quiet_active)            { FAIL("quiet_active should be true"); return; }
    PASS();
}

// ── T02: QUIET → NORMAL ──
static void test_quiet_to_normal(void)
{
    TEST("T02: QUIET short press -> NORMAL");
    mode_transition_result_t r = mode_transition_short_press(MODE_QUIET, true, MODE_NORMAL);
    if (r.user_mode != MODE_NORMAL) { FAIL("mode should be NORMAL"); return; }
    if (r.quiet_active)             { FAIL("quiet_active should be false"); return; }
    PASS();
}

// ── T03: quiet_active + NORMAL → NORMAL ──
static void test_quiet_active_to_normal(void)
{
    TEST("T03: quiet_active + NORMAL short press -> NORMAL");
    mode_transition_result_t r = mode_transition_short_press(MODE_NORMAL, true, MODE_NORMAL);
    if (r.user_mode != MODE_NORMAL) { FAIL("mode should be NORMAL"); return; }
    if (r.quiet_active)             { FAIL("quiet_active should be false"); return; }
    PASS();
}

// ── T04: NIGHT + !quiet → NIGHT + quiet (toggle quiet within night) ──
static void test_night_toggle_quiet_on(void)
{
    TEST("T04: NIGHT + !quiet -> stays NIGHT + quiet");
    mode_transition_result_t r = mode_transition_short_press(MODE_NIGHT, false, MODE_NORMAL);
    if (r.user_mode != MODE_NIGHT)  { FAIL("mode should stay NIGHT"); return; }
    if (!r.quiet_active)            { FAIL("quiet_active should toggle true"); return; }
    if (r.pre_night_mode != MODE_QUIET) { FAIL("pre_night_mode should be QUIET"); return; }
    PASS();
}

// ── T05: NIGHT + quiet → NIGHT + !quiet (toggle quiet off) ──
static void test_night_toggle_quiet_off(void)
{
    TEST("T05: NIGHT + quiet -> stays NIGHT + !quiet");
    mode_transition_result_t r = mode_transition_short_press(MODE_NIGHT, true, MODE_QUIET);
    if (r.user_mode != MODE_NIGHT)  { FAIL("mode should stay NIGHT"); return; }
    if (r.quiet_active)             { FAIL("quiet_active should toggle false"); return; }
    if (r.pre_night_mode != MODE_NORMAL) { FAIL("pre_night_mode should be NORMAL"); return; }
    PASS();
}

// ── T06: NORMAL + quiet_active → NORMAL (same as QUIET → NORMAL) ──
static void test_normal_quiet_active(void)
{
    TEST("T06: NORMAL + quiet_active -> NORMAL");
    mode_transition_result_t r = mode_transition_short_press(MODE_NORMAL, true, MODE_NORMAL);
    if (r.user_mode != MODE_NORMAL) { FAIL("mode should be NORMAL"); return; }
    if (r.quiet_active)             { FAIL("quiet_active should be false"); return; }
    PASS();
}

// ── T07: Cycle NORMAL → QUIET → NORMAL → QUIET ──
static void test_normal_quiet_cycle(void)
{
    TEST("T07: NORMAL -> QUIET -> NORMAL (double press)");
    mode_transition_result_t r1 = mode_transition_short_press(MODE_NORMAL, false, MODE_NORMAL);
    if (r1.user_mode != MODE_QUIET || !r1.quiet_active) {
        FAIL("first press: should be QUIET"); return;
    }
    mode_transition_result_t r2 = mode_transition_short_press(r1.user_mode, r1.quiet_active, MODE_NORMAL);
    if (r2.user_mode != MODE_NORMAL || r2.quiet_active) {
        FAIL("second press: should return to NORMAL"); return;
    }
    mode_transition_result_t r3 = mode_transition_short_press(r2.user_mode, r2.quiet_active, MODE_NORMAL);
    if (r3.user_mode != MODE_QUIET || !r3.quiet_active) {
        FAIL("third press: should be QUIET again"); return;
    }
    PASS();
}

// ── T08: NIGHT preserves across multiple presses ──
static void test_night_preserves(void)
{
    TEST("T08: NIGHT stays NIGHT across multiple presses");
    mode_transition_result_t r = mode_transition_short_press(MODE_NIGHT, false, MODE_NORMAL);
    if (r.user_mode != MODE_NIGHT) { FAIL("still NIGHT after 1 press"); return; }
    r = mode_transition_short_press(r.user_mode, r.quiet_active, r.pre_night_mode);
    if (r.user_mode != MODE_NIGHT) { FAIL("still NIGHT after 2 presses"); return; }
    r = mode_transition_short_press(r.user_mode, r.quiet_active, r.pre_night_mode);
    if (r.user_mode != MODE_NIGHT) { FAIL("still NIGHT after 3 presses"); return; }
    PASS();
}

// ── T09: NIGHT + quiet → pre_night_mode toggles with quiet_active ──
static void test_night_pre_night_mode_sync(void)
{
    TEST("T09: NIGHT pre_night_mode syncs with quiet toggle");
    mode_transition_result_t r = mode_transition_short_press(MODE_NIGHT, false, MODE_NORMAL);
    if (r.pre_night_mode != MODE_QUIET) { FAIL("quiet on -> pre_night=QUIET"); return; }
    r = mode_transition_short_press(r.user_mode, r.quiet_active, r.pre_night_mode);
    if (r.pre_night_mode != MODE_NORMAL) { FAIL("quiet off -> pre_night=NORMAL"); return; }
    PASS();
}

// ── T10: NIGHT does NOT exit to QUIET/NORMAL on press ──
static void test_night_no_exit(void)
{
    TEST("T10: NIGHT never exits to QUIET/NORMAL on short press");
    mode_transition_result_t r = mode_transition_short_press(MODE_NIGHT, false, MODE_NORMAL);
    for (int i = 0; i < 10; i++) {
        if (r.user_mode != MODE_NIGHT) {
            FAIL("NIGHT mode exited on press"); return;
        }
        r = mode_transition_short_press(r.user_mode, r.quiet_active, r.pre_night_mode);
    }
    PASS();
}

int main(void)
{
    printf("Button mode transition unit tests\n");
    printf("==================================\n\n");

    test_normal_to_quiet();
    test_quiet_to_normal();
    test_quiet_active_to_normal();
    test_night_toggle_quiet_on();
    test_night_toggle_quiet_off();
    test_normal_quiet_active();
    test_normal_quiet_cycle();
    test_night_preserves();
    test_night_pre_night_mode_sync();
    test_night_no_exit();

    printf("\n---\n");
    printf("PASS: %d  FAIL: %d  TOTAL: %d\n",
           s_pass, s_fail, s_pass + s_fail);

    return s_fail > 0 ? 1 : 0;
}
