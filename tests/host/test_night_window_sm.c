// Host unit tests for night_window_sm (pure logic, no FreeRTOS shim needed).
//
// Coverage map (state-model.md §3):
//   T01  normal window, before start -> not NIGHT
//   T02  normal window, at start     -> enter NIGHT
//   T03  normal window, at end-1     -> NIGHT
//   T04  normal window, at end       -> exit NIGHT
//   T05  cross-midnight, before start (daytime) -> not NIGHT
//   T06  cross-midnight, at start (22:00)       -> NIGHT
//   T07  cross-midnight, just before midnight   -> NIGHT
//   T08  cross-midnight, at midnight (00:00)    -> NIGHT
//   T09  cross-midnight, at end-1 (06:59)       -> NIGHT
//   T10  cross-midnight, at end (07:00)         -> exit NIGHT
//   T11  start == end: empty window, never NIGHT
//   T12  time_valid=false + not NIGHT -> no-op
//   T13  time_valid=false + NIGHT     -> exit, restore pre_night_mode
//   T14  enter NIGHT preserves pre_night_mode (NORMAL)
//   T15  enter NIGHT preserves pre_night_mode (QUIET)
//   T16  exit NIGHT restores pre_night_mode
//   T17  already NIGHT, time stays in window -> no-op
//   T18  already not NIGHT, time stays outside -> no-op
//   T19  full cycle: NORMAL -> NIGHT -> NORMAL
//   T20  full cycle: QUIET  -> NIGHT -> QUIET
//   T21  quiet_active never modified by night_window_sm_eval
//   T22  re-entry after exit: pre_night_mode re-saved correctly
//   T23  time_valid=false on second eval after NIGHT entry: exit to pre_night_mode
//   T24  local_minute at exact 1439 (last minute of day)
//   T25  start=end with cross-midnight formula would be "always"; verify empty semantics

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "night_window_sm.h"

static int s_pass = 0;
static int s_fail = 0;

#define TEST(name) do { printf("  %-50s ", name); } while (0)
#define PASS()     do { printf("PASS\n"); s_pass++; } while (0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); s_fail++; } while (0)

// Default config: 22:00–07:00 (cross-midnight). start=1320, end=420.
static night_window_config_t default_cfg(void)
{
    night_window_config_t c = { .night_start_min = 1320, .night_end_min = 420 };
    return c;
}

static night_window_sm_state_t state(night_window_mode_t mode,
                                     night_window_mode_t pre,
                                     bool quiet)
{
    night_window_sm_state_t s = {
        .user_mode      = mode,
        .pre_night_mode = pre,
        .quiet_active   = quiet,
    };
    return s;
}

// Compound-literal wrappers so &valid_time(x) / &invalid_time() are valid C.
// C does not allow & on a function-returned struct temporary; these macros
// produce C99 compound literals whose address CAN be taken.
#define valid_time(minute)  ((night_window_time_t){ .time_valid = true, .local_minute = (minute) })
#define invalid_time()      ((night_window_time_t){ .time_valid = false, .local_minute = 0 })

// ── T01: normal window, before start ──
static void test_normal_window_before_start(void)
{
    TEST("T01: normal window, before start -> not NIGHT");
    night_window_config_t c = { .night_start_min = 600, .night_end_min = 1200 };  // 10:00-20:00
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NORMAL, NIGHT_WINDOW_MODE_NORMAL, false);
    bool changed = night_window_sm_eval(&s, &valid_time(300), &c);  // 05:00
    if (changed)                                { FAIL("should not transition"); return; }
    if (s.user_mode != NIGHT_WINDOW_MODE_NORMAL){ FAIL("should stay NORMAL"); return; }
    PASS();
}

// ── T02: normal window, at start ──
static void test_normal_window_at_start(void)
{
    TEST("T02: normal window, at start -> NIGHT");
    night_window_config_t c = { .night_start_min = 600, .night_end_min = 1200 };
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NORMAL, NIGHT_WINDOW_MODE_NORMAL, false);
    bool changed = night_window_sm_eval(&s, &valid_time(600), &c);
    if (!changed)                                { FAIL("should transition"); return; }
    if (s.user_mode != NIGHT_WINDOW_MODE_NIGHT)  { FAIL("should be NIGHT"); return; }
    if (s.pre_night_mode != NIGHT_WINDOW_MODE_NORMAL) { FAIL("pre should be NORMAL"); return; }
    PASS();
}

// ── T03: normal window, at end-1 ──
static void test_normal_window_at_end_minus_one(void)
{
    TEST("T03: normal window, at end-1 -> still NIGHT");
    night_window_config_t c = { .night_start_min = 600, .night_end_min = 1200 };
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NIGHT, NIGHT_WINDOW_MODE_NORMAL, false);
    bool changed = night_window_sm_eval(&s, &valid_time(1199), &c);
    if (changed)                                { FAIL("should not transition at end-1"); return; }
    if (s.user_mode != NIGHT_WINDOW_MODE_NIGHT) { FAIL("should stay NIGHT"); return; }
    PASS();
}

// ── T04: normal window, at end ──
static void test_normal_window_at_end(void)
{
    TEST("T04: normal window, at end -> exit NIGHT");
    night_window_config_t c = { .night_start_min = 600, .night_end_min = 1200 };
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NIGHT, NIGHT_WINDOW_MODE_NORMAL, false);
    bool changed = night_window_sm_eval(&s, &valid_time(1200), &c);
    if (!changed)                                { FAIL("should transition"); return; }
    if (s.user_mode != NIGHT_WINDOW_MODE_NORMAL) { FAIL("should restore NORMAL"); return; }
    PASS();
}

// ── T05: cross-midnight, daytime (between end and start) ──
static void test_cross_midnight_daytime(void)
{
    TEST("T05: cross-midnight, daytime -> not NIGHT");
    night_window_config_t c = default_cfg();  // 22:00-07:00
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NORMAL, NIGHT_WINDOW_MODE_NORMAL, false);
    bool changed = night_window_sm_eval(&s, &valid_time(720), &c);  // 12:00
    if (changed)                                { FAIL("should not transition"); return; }
    if (s.user_mode != NIGHT_WINDOW_MODE_NORMAL){ FAIL("should stay NORMAL"); return; }
    PASS();
}

// ── T06: cross-midnight, at start (22:00) ──
static void test_cross_midnight_at_start(void)
{
    TEST("T06: cross-midnight, at start 22:00 -> NIGHT");
    night_window_config_t c = default_cfg();
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NORMAL, NIGHT_WINDOW_MODE_NORMAL, false);
    bool changed = night_window_sm_eval(&s, &valid_time(1320), &c);
    if (!changed)                                { FAIL("should transition"); return; }
    if (s.user_mode != NIGHT_WINDOW_MODE_NIGHT)  { FAIL("should be NIGHT"); return; }
    PASS();
}

// ── T07: cross-midnight, just before midnight (23:59 = 1439) ──
static void test_cross_midnight_before_midnight(void)
{
    TEST("T07: cross-midnight, 23:59 -> NIGHT");
    night_window_config_t c = default_cfg();
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NIGHT, NIGHT_WINDOW_MODE_NORMAL, false);
    bool changed = night_window_sm_eval(&s, &valid_time(1439), &c);
    if (changed)                                { FAIL("should stay NIGHT"); return; }
    if (s.user_mode != NIGHT_WINDOW_MODE_NIGHT) { FAIL("should be NIGHT"); return; }
    PASS();
}

// ── T08: cross-midnight, at midnight (00:00 = 0) ──
static void test_cross_midnight_at_midnight(void)
{
    TEST("T08: cross-midnight, 00:00 -> NIGHT");
    night_window_config_t c = default_cfg();
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NIGHT, NIGHT_WINDOW_MODE_NORMAL, false);
    bool changed = night_window_sm_eval(&s, &valid_time(0), &c);
    if (changed)                                { FAIL("should stay NIGHT"); return; }
    if (s.user_mode != NIGHT_WINDOW_MODE_NIGHT) { FAIL("should be NIGHT"); return; }
    PASS();
}

// ── T09: cross-midnight, at end-1 (06:59 = 419) ──
static void test_cross_midnight_at_end_minus_one(void)
{
    TEST("T09: cross-midnight, 06:59 -> NIGHT");
    night_window_config_t c = default_cfg();
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NIGHT, NIGHT_WINDOW_MODE_NORMAL, false);
    bool changed = night_window_sm_eval(&s, &valid_time(419), &c);
    if (changed)                                { FAIL("should stay NIGHT"); return; }
    if (s.user_mode != NIGHT_WINDOW_MODE_NIGHT) { FAIL("should be NIGHT"); return; }
    PASS();
}

// ── T10: cross-midnight, at end (07:00 = 420) ──
static void test_cross_midnight_at_end(void)
{
    TEST("T10: cross-midnight, 07:00 -> exit NIGHT");
    night_window_config_t c = default_cfg();
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NIGHT, NIGHT_WINDOW_MODE_NORMAL, false);
    bool changed = night_window_sm_eval(&s, &valid_time(420), &c);
    if (!changed)                                { FAIL("should transition"); return; }
    if (s.user_mode != NIGHT_WINDOW_MODE_NORMAL) { FAIL("should restore NORMAL"); return; }
    PASS();
}

// ── T11: start == end → empty window, never NIGHT ──
static void test_empty_window_never_night(void)
{
    TEST("T11: start == end -> empty window, never NIGHT");
    night_window_config_t c = { .night_start_min = 600, .night_end_min = 600 };
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NORMAL, NIGHT_WINDOW_MODE_NORMAL, false);

    // Try various times — none should enter NIGHT
    for (uint16_t t = 0; t < 1440; t += 60) {
        bool changed = night_window_sm_eval(&s, &valid_time(t), &c);
        if (changed || s.user_mode != NIGHT_WINDOW_MODE_NORMAL) {
            FAIL("empty window should not enter NIGHT"); return;
        }
    }

    // Even if somehow already NIGHT, time-valid eval should exit (not in window)
    s = state(NIGHT_WINDOW_MODE_NIGHT, NIGHT_WINDOW_MODE_NORMAL, false);
    bool changed = night_window_sm_eval(&s, &valid_time(600), &c);
    if (!changed)                                { FAIL("should exit NIGHT (not in window)"); return; }
    if (s.user_mode != NIGHT_WINDOW_MODE_NORMAL) { FAIL("should restore NORMAL"); return; }
    PASS();
}

// ── T12: time_valid=false + not NIGHT → no-op ──
static void test_invalid_time_no_op_when_not_night(void)
{
    TEST("T12: time invalid + not NIGHT -> no-op");
    night_window_config_t c = default_cfg();
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NORMAL, NIGHT_WINDOW_MODE_NORMAL, false);
    bool changed = night_window_sm_eval(&s, &invalid_time(), &c);
    if (changed)                                { FAIL("should not transition"); return; }
    if (s.user_mode != NIGHT_WINDOW_MODE_NORMAL){ FAIL("should stay NORMAL"); return; }
    PASS();
}

// ── T13: time_valid=false + NIGHT → exit, restore pre_night_mode ──
static void test_invalid_time_exits_night(void)
{
    TEST("T13: time invalid + NIGHT -> exit to pre_night_mode");
    night_window_config_t c = default_cfg();
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NIGHT, NIGHT_WINDOW_MODE_QUIET, false);
    bool changed = night_window_sm_eval(&s, &invalid_time(), &c);
    if (!changed)                                { FAIL("should transition"); return; }
    if (s.user_mode != NIGHT_WINDOW_MODE_QUIET)  { FAIL("should restore QUIET"); return; }
    // pre_night_mode intentionally preserved
    if (s.pre_night_mode != NIGHT_WINDOW_MODE_QUIET) { FAIL("pre_night_mode preserved"); return; }
    PASS();
}

// ── T14: enter NIGHT preserves pre_night_mode (NORMAL) ──
static void test_enter_night_preserves_normal(void)
{
    TEST("T14: enter NIGHT, pre_night_mode=NORMAL");
    night_window_config_t c = default_cfg();
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NORMAL, NIGHT_WINDOW_MODE_NORMAL, false);
    night_window_sm_eval(&s, &valid_time(1320), &c);
    if (s.pre_night_mode != NIGHT_WINDOW_MODE_NORMAL) { FAIL("pre should be NORMAL"); return; }
    PASS();
}

// ── T15: enter NIGHT preserves pre_night_mode (QUIET) ──
static void test_enter_night_preserves_quiet(void)
{
    TEST("T15: enter NIGHT, pre_night_mode=QUIET");
    night_window_config_t c = default_cfg();
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_QUIET, NIGHT_WINDOW_MODE_NORMAL, true);
    night_window_sm_eval(&s, &valid_time(1320), &c);
    if (s.pre_night_mode != NIGHT_WINDOW_MODE_QUIET) { FAIL("pre should be QUIET"); return; }
    PASS();
}

// ── T16: exit NIGHT restores pre_night_mode ──
static void test_exit_night_restores_pre(void)
{
    TEST("T16: exit NIGHT restores pre_night_mode");
    night_window_config_t c = default_cfg();
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NIGHT, NIGHT_WINDOW_MODE_QUIET, true);
    bool changed = night_window_sm_eval(&s, &valid_time(420), &c);  // 07:00
    if (!changed)                                { FAIL("should transition"); return; }
    if (s.user_mode != NIGHT_WINDOW_MODE_QUIET)  { FAIL("should restore QUIET"); return; }
    PASS();
}

// ── T17: already NIGHT, time stays in window → no-op ──
static void test_already_night_no_op(void)
{
    TEST("T17: already NIGHT, in window -> no-op");
    night_window_config_t c = default_cfg();
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NIGHT, NIGHT_WINDOW_MODE_NORMAL, false);
    bool changed = night_window_sm_eval(&s, &valid_time(1330), &c);
    if (changed)                                { FAIL("should not transition"); return; }
    if (s.user_mode != NIGHT_WINDOW_MODE_NIGHT) { FAIL("should stay NIGHT"); return; }
    PASS();
}

// ── T18: not NIGHT, time outside → no-op ──
static void test_not_night_outside_no_op(void)
{
    TEST("T18: not NIGHT, outside window -> no-op");
    night_window_config_t c = default_cfg();
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NORMAL, NIGHT_WINDOW_MODE_NORMAL, false);
    bool changed = night_window_sm_eval(&s, &valid_time(800), &c);  // 13:00
    if (changed)                                { FAIL("should not transition"); return; }
    PASS();
}

// ── T19: full cycle NORMAL -> NIGHT -> NORMAL ──
static void test_full_cycle_normal(void)
{
    TEST("T19: full cycle NORMAL -> NIGHT -> NORMAL");
    night_window_config_t c = default_cfg();
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NORMAL, NIGHT_WINDOW_MODE_NORMAL, false);

    bool changed = night_window_sm_eval(&s, &valid_time(1320), &c);
    if (!changed || s.user_mode != NIGHT_WINDOW_MODE_NIGHT) { FAIL("enter NIGHT"); return; }

    changed = night_window_sm_eval(&s, &valid_time(1330), &c);  // still in window
    if (changed) { FAIL("no-op in window"); return; }

    changed = night_window_sm_eval(&s, &valid_time(420), &c);   // 07:00 exit
    if (!changed || s.user_mode != NIGHT_WINDOW_MODE_NORMAL) { FAIL("exit NIGHT"); return; }
    PASS();
}

// ── T20: full cycle QUIET -> NIGHT -> QUIET ──
static void test_full_cycle_quiet(void)
{
    TEST("T20: full cycle QUIET -> NIGHT -> QUIET");
    night_window_config_t c = default_cfg();
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_QUIET, NIGHT_WINDOW_MODE_NORMAL, true);

    night_window_sm_eval(&s, &valid_time(1320), &c);
    if (s.user_mode != NIGHT_WINDOW_MODE_NIGHT)     { FAIL("enter NIGHT"); return; }
    if (s.pre_night_mode != NIGHT_WINDOW_MODE_QUIET){ FAIL("pre should be QUIET"); return; }

    night_window_sm_eval(&s, &valid_time(420), &c);
    if (s.user_mode != NIGHT_WINDOW_MODE_QUIET)     { FAIL("restore QUIET"); return; }
    PASS();
}

// ── T21: quiet_active never modified ──
static void test_quiet_active_never_modified(void)
{
    TEST("T21: quiet_active never modified by night_window_sm_eval");
    night_window_config_t c = default_cfg();

    // Enter NIGHT with quiet=true
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NORMAL, NIGHT_WINDOW_MODE_NORMAL, true);
    night_window_sm_eval(&s, &valid_time(1320), &c);
    if (!s.quiet_active) { FAIL("quiet_active should stay true on enter"); return; }

    // Exit NIGHT
    night_window_sm_eval(&s, &valid_time(420), &c);
    if (!s.quiet_active) { FAIL("quiet_active should stay true on exit"); return; }

    // Enter NIGHT with quiet=false
    s = state(NIGHT_WINDOW_MODE_NORMAL, NIGHT_WINDOW_MODE_NORMAL, false);
    night_window_sm_eval(&s, &valid_time(1320), &c);
    if (s.quiet_active) { FAIL("quiet_active should stay false on enter"); return; }
    PASS();
}

// ── T22: re-entry after exit: pre_night_mode re-saved ──
static void test_re_entry_after_exit(void)
{
    TEST("T22: re-entry after exit re-saves pre_night_mode");
    night_window_config_t c = default_cfg();
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NORMAL, NIGHT_WINDOW_MODE_NORMAL, false);

    // Day 1: enter NIGHT
    night_window_sm_eval(&s, &valid_time(1320), &c);
    if (s.pre_night_mode != NIGHT_WINDOW_MODE_NORMAL) { FAIL("pre=NORMAL on first entry"); return; }
    // Day 1: exit
    night_window_sm_eval(&s, &valid_time(420), &c);
    if (s.user_mode != NIGHT_WINDOW_MODE_NORMAL) { FAIL("restored NORMAL"); return; }

    // User toggles to QUIET during the day (mode_transition does this; here we simulate)
    s.user_mode = NIGHT_WINDOW_MODE_QUIET;
    s.quiet_active = true;
    // pre_night_mode is still NORMAL from last NIGHT cycle — but night_window_sm
    // will overwrite it on next entry, so its current value doesn't matter.

    // Day 2: enter NIGHT
    night_window_sm_eval(&s, &valid_time(1320), &c);
    if (s.user_mode != NIGHT_WINDOW_MODE_NIGHT)        { FAIL("enter NIGHT day 2"); return; }
    if (s.pre_night_mode != NIGHT_WINDOW_MODE_QUIET)   { FAIL("pre should be QUIET (current mode)"); return; }
    PASS();
}

// ── T23: time_valid=false on second eval after NIGHT entry: exit ──
static void test_time_lost_after_entry(void)
{
    TEST("T23: SNTP lost after NIGHT entry -> exit to pre_night_mode");
    night_window_config_t c = default_cfg();
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NORMAL, NIGHT_WINDOW_MODE_NORMAL, false);

    // SNTP valid, enter NIGHT
    night_window_sm_eval(&s, &valid_time(1320), &c);
    if (s.user_mode != NIGHT_WINDOW_MODE_NIGHT) { FAIL("should enter NIGHT"); return; }

    // SNTP lost: exit NIGHT, restore pre_night_mode (NORMAL)
    bool changed = night_window_sm_eval(&s, &invalid_time(), &c);
    if (!changed)                                { FAIL("should exit NIGHT"); return; }
    if (s.user_mode != NIGHT_WINDOW_MODE_NORMAL) { FAIL("should restore NORMAL"); return; }
    PASS();
}

// ── T24: local_minute = 1439 (last minute of day) ──
static void test_local_minute_1439(void)
{
    TEST("T24: local_minute=1439 in cross-midnight window -> NIGHT");
    night_window_config_t c = default_cfg();  // 22:00-07:00
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NORMAL, NIGHT_WINDOW_MODE_NORMAL, false);
    bool changed = night_window_sm_eval(&s, &valid_time(1439), &c);
    if (!changed)                                { FAIL("should enter NIGHT at 23:59"); return; }
    if (s.user_mode != NIGHT_WINDOW_MODE_NIGHT)  { FAIL("should be NIGHT"); return; }
    PASS();
}

// ── T25: start == end with cross-midnight formula would be "always" ──
// Verify our fixed empty-window semantics: start==end means NEVER, not ALWAYS.
static void test_start_equal_end_not_always(void)
{
    TEST("T25: start==end is NEVER (not ALWAYS)");
    night_window_config_t c = { .night_start_min = 0, .night_end_min = 0 };
    night_window_sm_state_t s = state(NIGHT_WINDOW_MODE_NORMAL, NIGHT_WINDOW_MODE_NORMAL, false);

    // If implementation mistakenly used cross-midnight formula with start==end,
    // every minute would be in-window. Verify that does NOT happen.
    bool any_enter = false;
    for (uint16_t t = 0; t < 1440; t += 120) {
        if (night_window_sm_eval(&s, &valid_time(t), &c)) {
            any_enter = true;
            break;
        }
    }
    if (any_enter) { FAIL("empty window must not enter NIGHT"); return; }
    PASS();
}

int main(void)
{
    printf("night_window_sm unit tests\n");
    printf("==========================\n\n");

    test_normal_window_before_start();
    test_normal_window_at_start();
    test_normal_window_at_end_minus_one();
    test_normal_window_at_end();
    test_cross_midnight_daytime();
    test_cross_midnight_at_start();
    test_cross_midnight_before_midnight();
    test_cross_midnight_at_midnight();
    test_cross_midnight_at_end_minus_one();
    test_cross_midnight_at_end();
    test_empty_window_never_night();
    test_invalid_time_no_op_when_not_night();
    test_invalid_time_exits_night();
    test_enter_night_preserves_normal();
    test_enter_night_preserves_quiet();
    test_exit_night_restores_pre();
    test_already_night_no_op();
    test_not_night_outside_no_op();
    test_full_cycle_normal();
    test_full_cycle_quiet();
    test_quiet_active_never_modified();
    test_re_entry_after_exit();
    test_time_lost_after_entry();
    test_local_minute_1439();
    test_start_equal_end_not_always();

    printf("\n---\n");
    printf("PASS: %d  FAIL: %d  TOTAL: %d\n",
           s_pass, s_fail, s_pass + s_fail);
    return s_fail > 0 ? 1 : 0;
}
