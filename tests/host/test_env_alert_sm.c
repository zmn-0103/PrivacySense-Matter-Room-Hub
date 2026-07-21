// Host unit tests for env_alert_sm (pure logic, no FreeRTOS shim needed).
//
// Coverage map (state-model.md §4):
//   T01  OK + alert cond, confirm timer not expired -> stays OK
//   T02  OK + alert cond, exactly confirm boundary  -> ACTIVE
//   T03  OK + alert cond, then drops to hysteresis  -> stays OK, timer reset
//   T04  OK + clear cond (no prior alert)           -> stays OK (no-op)
//   T05  ACTIVE + clear cond, exactly clear boundary -> OK
//   T06  ACTIVE + hysteresis band                   -> stays ACTIVE, timer reset
//   T07  ACTIVE + VACANT                             -> OK immediately
//   T08  OK + UNKNOWN occupancy                      -> freeze (stays OK, timer reset)
//   T09  ACTIVE + UNKNOWN occupancy                  -> freeze (stays ACTIVE, timer reset)
//   T10  ACTIVE + sensor offline                     -> freeze (stays ACTIVE)
//   T11  Freeze then resume: full confirm cycle required
//   T12  Direction reversal resets timer
//   T13  Temperature-only alert (humidity normal)
//   T14  Humidity-only alert (temperature normal)
//   T15  Both metrics above alert threshold
//   T16  Clear requires BOTH metrics below clear threshold
//   T17  Tick wrap-around (unsigned arithmetic safety)
//   T18  VACANT then OCCUPIED: confirm cycle restarts
//   T19  Initial ACTIVE state preserved by freeze
//   T20  Long confirm duration, no false transition
//   T21  Active refresh: re-entering alert cond during clear countdown resets to alert countdown

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "env_alert_sm.h"

static int s_pass = 0;
static int s_fail = 0;

#define TEST(name) do { printf("  %-50s ", name); } while (0)
#define PASS()     do { printf("PASS\n"); s_pass++; } while (0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); s_fail++; } while (0)

// Default config: 32.00 °C alert / 30.00 °C clear, 75.0 %RH alert / 70.0 %RH clear,
// 60 s confirm / 120 s clear.
static env_alert_config_t default_cfg(void)
{
    env_alert_config_t c = {
        .temp_alert_cc      = 3200,
        .temp_clear_cc      = 3000,
        .humid_alert_permil = 750,
        .humid_clear_permil = 700,
        .alert_confirm_ms   = 60000,
        .alert_clear_ms     = 120000,
    };
    return c;
}

static env_alert_input_t sample(int16_t temp_cc, uint16_t humid_permil)
{
    env_alert_input_t in = {
        .temp_cc       = temp_cc,
        .humid_permil  = humid_permil,
        .sensor_valid  = true,
    };
    return in;
}

// Compound-literal wrapper so &SAMPLE(a, b) is valid C.
// C does not allow & on a function-returned struct temporary; this macro
// produces a C99 compound literal whose address CAN be taken.
// NOTE: parameter names intentionally differ from struct field names to
// avoid accidental token expansion by the preprocessor.
#define SAMPLE(tc, hp) \
    ((env_alert_input_t){ .temp_cc = (tc), .humid_permil = (hp), .sensor_valid = true })

// ── T01: OK + alert condition, timer not yet expired ──
static void test_alert_confirm_not_expired(void)
{
    TEST("T01: OK + alert cond, confirm timer not expired");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_OK);
    env_alert_config_t cfg = default_cfg();
    env_alert_input_t in = sample(3300, 800);   // both above alert

    bool changed = env_alert_sm_eval(&sm, &in, false, false, 1000, &cfg);
    if (changed)                  { FAIL("unexpected transition"); return; }
    if (sm.state != ENV_ALERT_OK) { FAIL("should stay OK"); return; }
    if (!sm.timer_active)         { FAIL("timer should be active"); return; }
    if (!sm.counting_alert)       { FAIL("should be counting toward alert"); return; }

    // t=59999: 59999-1000 = 58999 < 60000
    changed = env_alert_sm_eval(&sm, &in, false, false, 59999, &cfg);
    if (changed)                  { FAIL("should not transition before 60000"); return; }
    if (sm.state != ENV_ALERT_OK) { FAIL("still OK"); return; }

    // t=61000: 61000-1000 = 60000 exactly
    changed = env_alert_sm_eval(&sm, &in, false, false, 61000, &cfg);
    if (!changed)                  { FAIL("should transition at 60000"); return; }
    if (sm.state != ENV_ALERT_ACTIVE) { FAIL("should be ACTIVE"); return; }
    if (sm.timer_active)           { FAIL("timer should be cleared after transition"); return; }
    PASS();
}

// ── T02: exact boundary 60000 ms ──
static void test_alert_exact_boundary(void)
{
    TEST("T02: alert transition at exact 60000 boundary");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_OK);
    env_alert_config_t cfg = default_cfg();
    env_alert_input_t in = sample(3300, 800);

    env_alert_sm_eval(&sm, &in, false, false, 0, &cfg);
    bool changed = env_alert_sm_eval(&sm, &in, false, false, 60000, &cfg);
    if (!changed)                  { FAIL("should transition at exact 60000"); return; }
    if (sm.state != ENV_ALERT_ACTIVE) { FAIL("should be ACTIVE"); return; }
    PASS();
}

// ── T03: alert cond then drops to hysteresis → stays OK, timer reset ──
static void test_hysteresis_resets_timer(void)
{
    TEST("T03: alert cond -> hysteresis, timer resets, stays OK");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_OK);
    env_alert_config_t cfg = default_cfg();

    // Start counting toward alert at t=0
    env_alert_sm_eval(&sm, &SAMPLE(3300, 800), false, false, 0, &cfg);
    if (!sm.timer_active) { FAIL("timer should be active"); return; }

    // Drop into hysteresis (between clear and alert): temp=3100 (30..32 band)
    env_alert_sm_eval(&sm, &SAMPLE(3100, 720), false, false, 30000, &cfg);
    if (sm.state != ENV_ALERT_OK) { FAIL("should stay OK"); return; }
    if (sm.timer_active)          { FAIL("timer should reset in hysteresis"); return; }

    // Re-enter alert at t=40000; should require FULL 60000 from here, not 30000
    env_alert_sm_eval(&sm, &SAMPLE(3300, 800), false, false, 40000, &cfg);
    // t=70000: 70000-40000 = 30000 < 60000 → no transition
    bool changed = env_alert_sm_eval(&sm, &SAMPLE(3300, 800), false, false, 70000, &cfg);
    if (changed)                  { FAIL("should NOT transition with only 30s of new timer"); return; }
    // t=100000: 100000-40000 = 60000 → transition
    changed = env_alert_sm_eval(&sm, &SAMPLE(3300, 800), false, false, 100000, &cfg);
    if (!changed)                 { FAIL("should transition at 100000"); return; }
    PASS();
}

// ── T04: OK + clear cond (no prior alert) → no-op ──
static void test_clear_from_ok_is_noop(void)
{
    TEST("T04: OK + clear cond (no prior alert), no transition");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_OK);
    env_alert_config_t cfg = default_cfg();

    // Already OK, clear condition present: should not "transition" to OK
    bool changed = env_alert_sm_eval(&sm, &SAMPLE(2500, 600), false, false, 0, &cfg);
    // Timer may start counting toward OK, but state unchanged
    if (changed)                  { FAIL("OK -> OK is not a transition"); return; }
    if (sm.state != ENV_ALERT_OK) { FAIL("should stay OK"); return; }

    // Even after clear_ms elapses, OK -> OK is not a "change"
    changed = env_alert_sm_eval(&sm, &SAMPLE(2500, 600), false, false, 120000, &cfg);
    if (changed)                  { FAIL("OK -> OK still not a transition"); return; }
    PASS();
}

// ── T05: ACTIVE + clear cond, exactly clear boundary ──
static void test_clear_transition(void)
{
    TEST("T05: ACTIVE + clear cond -> OK at clear boundary");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_ACTIVE);
    env_alert_config_t cfg = default_cfg();
    env_alert_input_t clear_in = sample(2500, 600);   // both below clear

    bool changed = env_alert_sm_eval(&sm, &clear_in, false, false, 0, &cfg);
    if (changed)                       { FAIL("should not transition immediately"); return; }
    if (sm.state != ENV_ALERT_ACTIVE) { FAIL("should stay ACTIVE"); return; }
    if (!sm.timer_active)              { FAIL("timer should be active"); return; }
    if (sm.counting_alert)             { FAIL("should be counting toward OK"); return; }

    // t=119999: 119999-0 = 119999 < 120000
    changed = env_alert_sm_eval(&sm, &clear_in, false, false, 119999, &cfg);
    if (changed)                       { FAIL("should not transition before 120000"); return; }

    // t=120000: exact boundary
    changed = env_alert_sm_eval(&sm, &clear_in, false, false, 120000, &cfg);
    if (!changed)                      { FAIL("should transition at 120000"); return; }
    if (sm.state != ENV_ALERT_OK)     { FAIL("should be OK"); return; }
    PASS();
}

// ── T06: ACTIVE + hysteresis → stays ACTIVE, timer reset ──
static void test_active_hysteresis(void)
{
    TEST("T06: ACTIVE + hysteresis band, stays ACTIVE, timer reset");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_ACTIVE);
    env_alert_config_t cfg = default_cfg();

    // Hysteresis: temp=3100 (30..32), humid=720 (70..75)
    env_alert_sm_eval(&sm, &SAMPLE(3100, 720), false, false, 0, &cfg);
    if (sm.state != ENV_ALERT_ACTIVE) { FAIL("should stay ACTIVE"); return; }
    if (sm.timer_active)              { FAIL("timer should reset"); return; }

    // Now clear cond appears; full 120000 should be required
    env_alert_sm_eval(&sm, &SAMPLE(2500, 600), false, false, 10000, &cfg);
    bool changed = env_alert_sm_eval(&sm, &SAMPLE(2500, 600), false, false, 129999, &cfg);
    if (changed) { FAIL("should not transition with only 119999 ms"); return; }
    changed = env_alert_sm_eval(&sm, &SAMPLE(2500, 600), false, false, 130000, &cfg);
    if (!changed) { FAIL("should transition at 130000"); return; }
    PASS();
}

// ── T07: ACTIVE + VACANT → OK immediately ──
static void test_vacant_immediate_clear(void)
{
    TEST("T07: ACTIVE + VACANT -> OK immediately");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_ACTIVE);
    env_alert_config_t cfg = default_cfg();
    env_alert_input_t in = sample(3300, 800);

    // Mid-countdown toward alert (irrelevant): VACANT clears immediately
    bool changed = env_alert_sm_eval(&sm, &in, true, false, 0, &cfg);
    if (!changed)                  { FAIL("should transition ACTIVE -> OK"); return; }
    if (sm.state != ENV_ALERT_OK) { FAIL("should be OK"); return; }
    if (sm.timer_active)          { FAIL("timer should be reset"); return; }
    PASS();
}

// ── T08: OK + UNKNOWN → freeze (stays OK, timer reset) ──
static void test_unknown_freeze_from_ok(void)
{
    TEST("T08: OK + UNKNOWN occupancy -> freeze");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_OK);
    env_alert_config_t cfg = default_cfg();

    // Build some timer state first
    env_alert_sm_eval(&sm, &SAMPLE(3300, 800), false, false, 0, &cfg);
    if (!sm.timer_active) { FAIL("timer should be active before freeze"); return; }

    // UNKNOWN: freeze
    bool changed = env_alert_sm_eval(&sm, &SAMPLE(3300, 800), false, true, 30000, &cfg);
    if (changed)                  { FAIL("freeze is not a transition"); return; }
    if (sm.state != ENV_ALERT_OK) { FAIL("should stay OK"); return; }
    if (sm.timer_active)          { FAIL("timer should be reset on freeze"); return; }
    PASS();
}

// ── T09: ACTIVE + UNKNOWN → freeze (stays ACTIVE, timer reset) ──
static void test_unknown_freeze_from_active(void)
{
    TEST("T09: ACTIVE + UNKNOWN occupancy -> freeze, stays ACTIVE");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_ACTIVE);
    env_alert_config_t cfg = default_cfg();

    // Start clear countdown
    env_alert_sm_eval(&sm, &SAMPLE(2500, 600), false, false, 0, &cfg);
    if (!sm.timer_active) { FAIL("timer should be active"); return; }

    // UNKNOWN: freeze
    bool changed = env_alert_sm_eval(&sm, &SAMPLE(2500, 600), false, true, 50000, &cfg);
    if (changed)                       { FAIL("freeze is not a transition"); return; }
    if (sm.state != ENV_ALERT_ACTIVE) { FAIL("should stay ACTIVE"); return; }
    if (sm.timer_active)              { FAIL("timer should be reset"); return; }
    PASS();
}

// ── T10: ACTIVE + sensor offline → freeze ──
static void test_sensor_offline_freeze(void)
{
    TEST("T10: ACTIVE + sensor offline -> freeze, stays ACTIVE");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_ACTIVE);
    env_alert_config_t cfg = default_cfg();

    env_alert_input_t offline = { .temp_cc = 0, .humid_permil = 0, .sensor_valid = false };
    bool changed = env_alert_sm_eval(&sm, &offline, false, false, 0, &cfg);
    if (changed)                       { FAIL("freeze is not a transition"); return; }
    if (sm.state != ENV_ALERT_ACTIVE) { FAIL("should stay ACTIVE"); return; }
    if (sm.timer_active)              { FAIL("timer should be reset"); return; }
    PASS();
}

// ── T11: Freeze then resume: full confirm cycle required ──
static void test_freeze_resume_requires_full_cycle(void)
{
    TEST("T11: freeze during countdown, resume requires full cycle");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_OK);
    env_alert_config_t cfg = default_cfg();
    env_alert_input_t alert_in = sample(3300, 800);

    // 40 s of alert cond
    env_alert_sm_eval(&sm, &alert_in, false, false, 0, &cfg);
    env_alert_sm_eval(&sm, &alert_in, false, false, 40000, &cfg);
    if (sm.state != ENV_ALERT_OK) { FAIL("should still be OK"); return; }

    // 5 s UNKNOWN freeze
    env_alert_sm_eval(&sm, &alert_in, false, true, 45000, &cfg);
    if (sm.timer_active) { FAIL("timer reset by freeze"); return; }

    // Resume at t=50000; need full 60000 from 50000 = 110000
    env_alert_sm_eval(&sm, &alert_in, false, false, 50000, &cfg);
    bool changed = env_alert_sm_eval(&sm, &alert_in, false, false, 109999, &cfg);
    if (changed) { FAIL("should not transition: only 59999 ms since resume"); return; }
    changed = env_alert_sm_eval(&sm, &alert_in, false, false, 110000, &cfg);
    if (!changed) { FAIL("should transition at 110000"); return; }
    if (sm.state != ENV_ALERT_ACTIVE) { FAIL("should be ACTIVE"); return; }
    PASS();
}

// ── T12: Direction reversal resets timer ──
static void test_direction_reversal(void)
{
    TEST("T12: alert -> clear direction reversal resets timer");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_OK);
    env_alert_config_t cfg = default_cfg();

    env_alert_sm_eval(&sm, &SAMPLE(3300, 800), false, false, 0, &cfg);   // alert cond
    env_alert_sm_eval(&sm, &SAMPLE(3300, 800), false, false, 30000, &cfg);   // 30 s in
    // Switch to clear cond at t=40000
    env_alert_sm_eval(&sm, &SAMPLE(2500, 600), false, false, 40000, &cfg);
    if (sm.counting_alert) { FAIL("should be counting toward OK now"); return; }
    if (!sm.timer_active)  { FAIL("timer should be active"); return; }

    // OK is not a transition from OK, so no change expected. But the timer
    // direction reset is the key behavior. Now switch back to alert at t=50000.
    env_alert_sm_eval(&sm, &SAMPLE(3300, 800), false, false, 50000, &cfg);
    if (!sm.counting_alert) { FAIL("should be counting toward alert again"); return; }

    // Need full 60000 from t=50000 = 110000
    bool changed = env_alert_sm_eval(&sm, &SAMPLE(3300, 800), false, false, 109999, &cfg);
    if (changed) { FAIL("should not transition yet"); return; }
    changed = env_alert_sm_eval(&sm, &SAMPLE(3300, 800), false, false, 110000, &cfg);
    if (!changed) { FAIL("should transition at 110000"); return; }
    PASS();
}

// ── T13: Temperature-only alert (humidity normal) ──
static void test_temp_only_alert(void)
{
    TEST("T13: temp above alert, humidity normal -> alert");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_OK);
    env_alert_config_t cfg = default_cfg();
    env_alert_input_t in = sample(3300, 500);   // temp alert, humid normal

    env_alert_sm_eval(&sm, &in, false, false, 0, &cfg);
    bool changed = env_alert_sm_eval(&sm, &in, false, false, 60000, &cfg);
    if (!changed)                  { FAIL("temp-only should trigger alert"); return; }
    if (sm.state != ENV_ALERT_ACTIVE) { FAIL("should be ACTIVE"); return; }
    PASS();
}

// ── T14: Humidity-only alert (temperature normal) ──
static void test_humid_only_alert(void)
{
    TEST("T14: humidity above alert, temp normal -> alert");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_OK);
    env_alert_config_t cfg = default_cfg();
    env_alert_input_t in = sample(2500, 800);   // temp normal, humid alert

    env_alert_sm_eval(&sm, &in, false, false, 0, &cfg);
    bool changed = env_alert_sm_eval(&sm, &in, false, false, 60000, &cfg);
    if (!changed)                  { FAIL("humid-only should trigger alert"); return; }
    if (sm.state != ENV_ALERT_ACTIVE) { FAIL("should be ACTIVE"); return; }
    PASS();
}

// ── T15: Both metrics above alert ──
static void test_both_above_alert(void)
{
    TEST("T15: both metrics above alert -> alert");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_OK);
    env_alert_config_t cfg = default_cfg();
    env_alert_input_t in = sample(3500, 900);

    env_alert_sm_eval(&sm, &in, false, false, 0, &cfg);
    bool changed = env_alert_sm_eval(&sm, &in, false, false, 60000, &cfg);
    if (!changed) { FAIL("both above should trigger alert"); return; }
    PASS();
}

// ── T16: Clear requires BOTH metrics below clear threshold ──
static void test_clear_requires_both(void)
{
    TEST("T16: clear requires BOTH metrics below clear threshold");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_ACTIVE);
    env_alert_config_t cfg = default_cfg();

    // temp below clear, humidity in hysteresis (720: 700 < 720 < 750)
    // → NOT clear condition → hysteresis → timer reset, stays ACTIVE
    env_alert_sm_eval(&sm, &SAMPLE(2500, 720), false, false, 0, &cfg);
    if (sm.timer_active) { FAIL("hysteresis should reset timer"); return; }

    bool changed = env_alert_sm_eval(&sm, &SAMPLE(2500, 720), false, false, 120000, &cfg);
    if (changed)                       { FAIL("should not transition: humid not below clear"); return; }
    if (sm.state != ENV_ALERT_ACTIVE) { FAIL("should stay ACTIVE"); return; }

    // Now both below clear: should clear after 120000
    env_alert_sm_eval(&sm, &SAMPLE(2500, 600), false, false, 1000, &cfg);
    changed = env_alert_sm_eval(&sm, &SAMPLE(2500, 600), false, false, 121000, &cfg);
    if (!changed) { FAIL("should clear when both below"); return; }
    PASS();
}

// ── T17: Tick wrap-around ──
static void test_tick_wraparound(void)
{
    TEST("T17: tick wrap-around, unsigned arithmetic safe");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_OK);
    env_alert_config_t cfg = default_cfg();
    env_alert_input_t in = sample(3300, 800);

    // Start timer near wrap (UINT32_MAX - 5000)
    uint32_t near_wrap = 0xFFFFEC76U;   // UINT32_MAX - 5000
    env_alert_sm_eval(&sm, &in, false, false, near_wrap, &cfg);
    if (!sm.timer_active) { FAIL("timer should be active"); return; }
    if (sm.timer_start_ms != near_wrap) { FAIL("timer_start captured"); return; }

    // After wrap: now = 55000 (i.e., 5000 + 55000 = 60000 ms elapsed)
    bool changed = env_alert_sm_eval(&sm, &in, false, false, 55000, &cfg);
    if (!changed)                  { FAIL("should transition across wrap"); return; }
    if (sm.state != ENV_ALERT_ACTIVE) { FAIL("should be ACTIVE"); return; }
    PASS();
}

// ── T18: VACANT then OCCUPIED: confirm cycle restarts ──
static void test_vacant_then_occupied_restart(void)
{
    TEST("T18: VACANT clears, OCCUPIED restarts full confirm cycle");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_ACTIVE);
    env_alert_config_t cfg = default_cfg();
    env_alert_input_t alert_in = sample(3300, 800);

    // VACANT at t=0 clears immediately
    env_alert_sm_eval(&sm, &alert_in, true, false, 0, &cfg);
    if (sm.state != ENV_ALERT_OK) { FAIL("should be OK after VACANT"); return; }

    // OCCUPIED with alert cond at t=1000; need full 60000 from t=1000 = 61000
    env_alert_sm_eval(&sm, &alert_in, false, false, 1000, &cfg);
    bool changed = env_alert_sm_eval(&sm, &alert_in, false, false, 60999, &cfg);
    if (changed) { FAIL("should not transition before 61000"); return; }
    changed = env_alert_sm_eval(&sm, &alert_in, false, false, 61000, &cfg);
    if (!changed) { FAIL("should transition at 61000"); return; }
    PASS();
}

// ── T19: Initial ACTIVE preserved by freeze ──
static void test_initial_active_preserved(void)
{
    TEST("T19: initial ACTIVE + sensor offline preserves ACTIVE");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_ACTIVE);
    env_alert_config_t cfg = default_cfg();

    env_alert_input_t offline = { .sensor_valid = false };
    for (uint32_t t = 0; t < 600000; t += 1000) {
        env_alert_sm_eval(&sm, &offline, false, false, t, &cfg);
        if (sm.state != ENV_ALERT_ACTIVE) {
            FAIL("ACTIVE should be preserved across freeze"); return;
        }
    }
    PASS();
}

// ── T20: Long confirm duration, no false transition ──
static void test_long_confirm_no_false(void)
{
    TEST("T20: long confirm, no false transition");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_OK);
    env_alert_config_t cfg = default_cfg();
    env_alert_input_t in = sample(3300, 800);

    for (uint32_t t = 0; t < 59999; t += 1000) {
        if (env_alert_sm_eval(&sm, &in, false, false, t, &cfg)) {
            FAIL("should not transition before 60000"); return;
        }
    }
    if (sm.state != ENV_ALERT_OK) { FAIL("should still be OK"); return; }
    PASS();
}

// ── T21: Active refresh — alert cond during clear countdown ──
static void test_alert_refresh_during_clear(void)
{
    TEST("T21: alert cond during clear countdown resets to alert countdown");
    env_alert_sm_t sm;
    env_alert_sm_init(&sm, ENV_ALERT_ACTIVE);
    env_alert_config_t cfg = default_cfg();

    // Start clear countdown at t=0
    env_alert_sm_eval(&sm, &SAMPLE(2500, 600), false, false, 0, &cfg);
    if (!sm.timer_active || sm.counting_alert) { FAIL("counting toward OK"); return; }

    // 50 s into clear countdown, alert condition reappears
    env_alert_sm_eval(&sm, &SAMPLE(3300, 800), false, false, 50000, &cfg);
    if (!sm.counting_alert) { FAIL("should be counting toward alert again"); return; }
    // Timer restarted at t=50000; need 60000 from there = 110000 to switch to ACTIVE
    // (ACTIVE -> ACTIVE is not a transition, but the timer state matters for the next clear)
    bool changed = env_alert_sm_eval(&sm, &SAMPLE(3300, 800), false, false, 110000, &cfg);
    // Already ACTIVE, so no state change reported
    if (changed) { FAIL("ACTIVE -> ACTIVE is not a transition"); return; }
    if (sm.state != ENV_ALERT_ACTIVE) { FAIL("should stay ACTIVE"); return; }
    if (sm.timer_active) { FAIL("timer should be cleared after ACTIVE re-commit"); return; }

    // Now clear cond; should require full 120000
    env_alert_sm_eval(&sm, &SAMPLE(2500, 600), false, false, 110000, &cfg);
    changed = env_alert_sm_eval(&sm, &SAMPLE(2500, 600), false, false, 229999, &cfg);
    if (changed) { FAIL("should not clear before 230000"); return; }
    changed = env_alert_sm_eval(&sm, &SAMPLE(2500, 600), false, false, 230000, &cfg);
    if (!changed) { FAIL("should clear at 230000"); return; }
    PASS();
}

int main(void)
{
    printf("env_alert_sm unit tests\n");
    printf("========================\n\n");

    test_alert_confirm_not_expired();
    test_alert_exact_boundary();
    test_hysteresis_resets_timer();
    test_clear_from_ok_is_noop();
    test_clear_transition();
    test_active_hysteresis();
    test_vacant_immediate_clear();
    test_unknown_freeze_from_ok();
    test_unknown_freeze_from_active();
    test_sensor_offline_freeze();
    test_freeze_resume_requires_full_cycle();
    test_direction_reversal();
    test_temp_only_alert();
    test_humid_only_alert();
    test_both_above_alert();
    test_clear_requires_both();
    test_tick_wraparound();
    test_vacant_then_occupied_restart();
    test_initial_active_preserved();
    test_long_confirm_no_false();
    test_alert_refresh_during_clear();

    printf("\n---\n");
    printf("PASS: %d  FAIL: %d  TOTAL: %d\n",
           s_pass, s_fail, s_pass + s_fail);
    return s_fail > 0 ? 1 : 0;
}
