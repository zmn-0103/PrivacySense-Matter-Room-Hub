#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "occ_sm.h"

static int s_pass = 0;
static int s_fail = 0;

#define TEST(name) do { printf("  %-50s ", name); } while (0)
#define PASS()     do { printf("PASS\n"); s_pass++; } while (0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); s_fail++; } while (0)

// ── T01: VACANT + confirm timer not yet expired ──
static void test_entry_not_confirmed(void)
{
    TEST("T01: VACANT + brief occupation, timer not expired");
    occ_sm_t sm;
    occ_sm_init(&sm, OCC_VACANT);

    // Target present at t=1000
    bool trans = occ_sm_eval(&sm, true, 1000, 2000, 120000);
    if (trans)                { FAIL("unexpected transition"); return; }
    if (sm.state != OCC_VACANT) { FAIL("should remain VACANT"); return; }
    if (!sm.timer_active)     { FAIL("timer should be active"); return; }

    // Still present at t=2999 (just before confirm: 2999-1000 = 1999 < 2000)
    trans = occ_sm_eval(&sm, true, 2999, 2000, 120000);
    if (trans)                { FAIL("unexpected transition at 2999"); return; }
    if (sm.state != OCC_VACANT) { FAIL("should still be VACANT"); return; }

    // Present at t=3000 (exactly confirm: 3000-1000 = 2000)
    trans = occ_sm_eval(&sm, true, 3000, 2000, 120000);
    if (!trans)               { FAIL("should transition at 3000"); return; }
    if (sm.state != OCC_OCCUPIED){ FAIL("should be OCCUPIED"); return; }

    PASS();
}

// ── T02: VACANT → timer start → direction change resets timer ──
static void test_direction_reset(void)
{
    TEST("T02: VACANT, direction change resets timer");
    occ_sm_t sm;
    occ_sm_init(&sm, OCC_VACANT);

    occ_sm_eval(&sm, true, 100, 2000, 120000);  // start timer toward OCCUPIED
    // Before confirm, target becomes absent
    occ_sm_eval(&sm, false, 500, 2000, 120000); // should cancel timer
    if (sm.timer_active) { FAIL("timer should be cancelled"); return; }
    if (sm.state != OCC_VACANT) { FAIL("should stay VACANT"); return; }
    PASS();
}

// ── T03: OCCUPIED + exit timer ──
static void test_exit_delay(void)
{
    TEST("T03: OCCUPIED + exit delay not yet expired");
    occ_sm_t sm;
    occ_sm_init(&sm, OCC_OCCUPIED);

    // Target absent at t=100
    bool trans = occ_sm_eval(&sm, false, 100, 2000, 120000);
    if (trans)                { FAIL("unexpected transition"); return; }
    if (sm.state != OCC_OCCUPIED){ FAIL("should remain OCCUPIED"); return; }
    if (!sm.timer_active)     { FAIL("timer should be active"); return; }

    // Still absent at t=120099 (just before exit: 120099-100 = 119999 < 120000)
    trans = occ_sm_eval(&sm, false, 120099, 2000, 120000);
    if (trans)                { FAIL("unexpected at 120099"); return; }
    if (sm.state != OCC_OCCUPIED){ FAIL("should still be OCCUPIED"); return; }

    // Absent at t=120100 (exactly exit: 120100-100 = 120000)
    trans = occ_sm_eval(&sm, false, 120100, 2000, 120000);
    if (!trans)               { FAIL("should transition at 120100"); return; }
    if (sm.state != OCC_VACANT)  { FAIL("should be VACANT"); return; }
    PASS();
}

// ── T04: OCCUPIED → target returns before exit → timer cancelled ──
static void test_exit_cancelled(void)
{
    TEST("T04: OCCUPIED, target returns before exit");
    occ_sm_t sm;
    occ_sm_init(&sm, OCC_OCCUPIED);

    occ_sm_eval(&sm, false, 100, 2000, 120000); // start exit timer
    // Target returns before exit
    occ_sm_eval(&sm, true, 50000, 2000, 120000); // should cancel
    if (sm.timer_active) { FAIL("timer should be cancelled"); return; }
    if (sm.state != OCC_OCCUPIED) { FAIL("should stay OCCUPIED"); return; }
    PASS();
}

// ── T05: UNKNOWN recovery → VACANT (no target) ──
static void test_unknown_recover_vacant(void)
{
    TEST("T05: UNKNOWN recovery -> VACANT");
    occ_sm_t sm;
    occ_sm_init(&sm, OCC_UNKNOWN);

    // No target at t=1000
    bool trans = occ_sm_eval(&sm, false, 1000, 2000, 120000);
    if (trans)                { FAIL("unexpected early transition"); return; }
    if (sm.state != OCC_UNKNOWN){ FAIL("should remain UNKNOWN"); return; }
    if (!sm.timer_active)     { FAIL("timer should be active"); return; }

    // Still no target at t=130000 (exit_delay = 120000, started at 1000 → elapsed 129000 > 120000)
    // Actually let me use a simpler timeout value
    trans = occ_sm_eval(&sm, false, 1000 + 120000, 2000, 120000);
    if (!trans)               { FAIL("should transition at exit_delay"); return; }
    if (sm.state != OCC_VACANT)  { FAIL("should be VACANT"); return; }
    PASS();
}

// ── T06: UNKNOWN recovery → OCCUPIED (target present) ──
static void test_unknown_recover_occupied(void)
{
    TEST("T06: UNKNOWN recovery -> OCCUPIED");
    occ_sm_t sm;
    occ_sm_init(&sm, OCC_UNKNOWN);

    occ_sm_eval(&sm, true, 100, 2000, 120000);  // start toward OCCUPIED
    bool trans = occ_sm_eval(&sm, true, 2100, 2000, 120000); // entry_confirm elapsed
    if (!trans)               { FAIL("should transition at entry_confirm"); return; }
    if (sm.state != OCC_OCCUPIED){ FAIL("should be OCCUPIED"); return; }
    PASS();
}

// ── T07: UNKNOWN recovery direction reversal ──
static void test_unknown_direction_reversal(void)
{
    TEST("T07: UNKNOWN, direction reversal resets timer");
    occ_sm_t sm;
    occ_sm_init(&sm, OCC_UNKNOWN);

    occ_sm_eval(&sm, true, 100, 2000, 120000);  // counting toward OCCUPIED
    // Target gone → direction flip
    occ_sm_eval(&sm, false, 500, 2000, 120000);
    // Should have reset timer toward VACANT. At t=500+10=510, elapsed=10 < 120000
    bool trans = occ_sm_eval(&sm, false, 510, 2000, 120000);
    if (trans) { FAIL("too early after direction flip"); return; }

    // After exit_delay from flip time (500 + 120000 = 120500)
    trans = occ_sm_eval(&sm, false, 120500, 2000, 120000);
    if (!trans) { FAIL("should transition after exit_delay from flip"); return; }
    if (sm.state != OCC_VACANT) { FAIL("should be VACANT"); return; }
    PASS();
}

// ── T08: UNKNOWN → initial target_present idle (no direction) ──
static void test_unknown_initial_values(void)
{
    TEST("T08: UNKNOWN, initial timer state");
    occ_sm_t sm;
    occ_sm_init(&sm, OCC_UNKNOWN);

    if (sm.state != OCC_UNKNOWN)    { FAIL("state should be UNKNOWN"); return; }
    if (sm.timer_active)             { FAIL("timer should be inactive"); return; }
    if (sm.timer_start_ms != 0)      { FAIL("timer start should be 0"); return; }
    PASS();
}

// ── T09: Tick wraparound (simulated) ──
static void test_tick_wraparound(void)
{
    TEST("T09: tick wraparound, timer value near UINT32_MAX");
    occ_sm_t sm;
    occ_sm_init(&sm, OCC_VACANT);

    uint32_t near_wrap = 0xFFFFFFF0U;
    // Target present with timer near wrap
    occ_sm_eval(&sm, true, near_wrap, 2000, 120000);
    if (!sm.timer_active) { FAIL("timer should be active"); return; }
    if (sm.timer_start_ms != near_wrap) { FAIL("timer_start should be near_wrap"); return; }

    // Simulate "after wrap": now = 10, timer_start = near_wrap → elapsed = 10 + (UINT32_MAX - near_wrap + 1)
    // Actually occ_sm_eval uses (now_ms - timer_start_ms) unsigned, which is correct for wrap.
    // Let's just check that no wrong transition happens
    occ_sm_eval(&sm, true, 10, 2000, 120000); // after wrap, still present
    if (sm.state != OCC_VACANT) {
        FAIL("should stay VACANT (tick wraparound needs correct unsigned math)");
        return;
    }
    PASS();
}

// ── T10: Rapid direction change (glitch filtering) ──
static void test_rapid_glitch(void)
{
    TEST("T10: rapid direction change (glitch)");
    occ_sm_t sm;
    occ_sm_init(&sm, OCC_OCCUPIED);

    // Target flickers off then back on within one eval
    occ_sm_eval(&sm, false, 100, 2000, 120000);  // start exit timer
    occ_sm_eval(&sm, true, 101, 2000, 120000);   // target back → cancel
    if (sm.timer_active) { FAIL("timer should be cancelled"); return; }
    if (sm.state != OCC_OCCUPIED) { FAIL("should stay OCCUPIED"); return; }

    // And the exit countdown restarts fresh
    occ_sm_eval(&sm, false, 200, 2000, 120000);  // restart exit timer
    // Should NOT transition at 100 + exit_delay, but at 200 + exit_delay
    occ_sm_eval(&sm, false, 100 + 120000, 2000, 120000); // old expiry
    if (sm.state != OCC_OCCUPIED) { FAIL("should not use old timer expiry"); return; }
    occ_sm_eval(&sm, false, 200 + 120000, 2000, 120000); // new expiry
    if (sm.state != OCC_VACANT) { FAIL("should transition at new expiry"); return; }
    PASS();
}

// ── T11: VACANT → timer ≥ 0 check ──
static void test_timer_exact_boundary(void)
{
    TEST("T11: VACANT confirm at exact 2000 ms boundary");
    occ_sm_t sm;
    occ_sm_init(&sm, OCC_VACANT);

    occ_sm_eval(&sm, true, 0, 2000, 120000);
    // exactly at boundary
    bool trans = occ_sm_eval(&sm, true, 2000, 2000, 120000);
    if (!trans) { FAIL("should transition at exact 2000"); return; }
    if (sm.state != OCC_OCCUPIED) { FAIL("should be OCCUPIED"); return; }
    PASS();
}

// ── T12: OCCUPIED exit at exact 120000 boundary ──
static void test_exit_exact_boundary(void)
{
    TEST("T12: OCCUPIED exit at exact 120000 boundary");
    occ_sm_t sm;
    occ_sm_init(&sm, OCC_OCCUPIED);

    occ_sm_eval(&sm, false, 0, 2000, 120000);
    bool trans = occ_sm_eval(&sm, false, 120000, 2000, 120000);
    if (!trans) { FAIL("should transition at exact 120000"); return; }
    if (sm.state != OCC_VACANT) { FAIL("should be VACANT"); return; }
    PASS();
}

// ── T13: init VACANT → no transition on first eval with idle ──
static void test_idle_vacant_stays_vacant(void)
{
    TEST("T13: VACANT + target absent = stay VACANT");
    occ_sm_t sm;
    occ_sm_init(&sm, OCC_VACANT);

    bool trans = occ_sm_eval(&sm, false, 0, 2000, 120000);
    if (trans) { FAIL("should not transition"); return; }
    if (sm.state != OCC_VACANT) { FAIL("should be VACANT"); return; }
    if (sm.timer_active) { FAIL("timer should stay inactive"); return; }
    PASS();
}

// ── T14: UNKNOWN recovery from OCCUPIED ──
static void test_unknown_from_occupied(void)
{
    TEST("T14: UNKNOWN, counting toward OCCUPIED entry");
    occ_sm_t sm;
    occ_sm_init(&sm, OCC_UNKNOWN);

    // Target present: count toward OCCUPIED
    occ_sm_eval(&sm, true, 0, 2000, 120000);
    bool trans = occ_sm_eval(&sm, true, 2000, 2000, 120000);
    if (!trans || sm.state != OCC_OCCUPIED) {
        FAIL("should recover to OCCUPIED from UNKNOWN with target");
        return;
    }
    PASS();
}

// ── T15: long idle in VACANT ──
static void test_long_idle_vacant(void)
{
    TEST("T15: VACANT, long idle (1000 evals), stays VACANT");
    occ_sm_t sm;
    occ_sm_init(&sm, OCC_VACANT);

    for (uint32_t t = 0; t < 1000000; t += 1000) {
        if (occ_sm_eval(&sm, false, t, 2000, 120000)) {
            FAIL("should not transition during long idle");
            return;
        }
    }
    if (sm.state != OCC_VACANT) { FAIL("should stay VACANT"); return; }
    PASS();
}

// ── T16: Candidate copy — timer restarts on commit failure ──
static void test_candidate_restarts_timer(void)
{
    TEST("T16: commit failure restarts confirm timer from next frame");
    occ_sm_t sm;
    occ_sm_init(&sm, OCC_VACANT);

    // First attempt on candidate reaches transition at t=2000
    occ_sm_t c = sm;
    occ_sm_eval(&c, true, 0, 2000, 120000);
    occ_sm_eval(&c, true, 2000, 2000, 120000);
    if (c.state != OCC_OCCUPIED) { FAIL("candidate should reach OCCUPIED"); return; }
    // Commit FAILED: sm is unchanged (VACANT, timer inactive)

    // Retry: timer starts fresh from 2500
    c = sm;
    occ_sm_eval(&c, true, 2500, 2000, 120000);
    // c should still be VACANT (elapsed = 2500-2500 = 0 < 2000)
    if (c.state != OCC_VACANT) { FAIL("should restart timer, not transition yet"); return; }
    if (!c.timer_active) { FAIL("timer should be active after restart"); return; }

    // Complete the new timer
    occ_sm_eval(&c, true, 4500, 2000, 120000);
    if (c.state != OCC_OCCUPIED) { FAIL("should transition after restarted timer"); return; }

    // Now commit succeeds
    sm = c;
    if (sm.state != OCC_OCCUPIED) { FAIL("final state should be OCCUPIED"); return; }
    PASS();
}

// ── T17: Candidate copy — UNKNOWN timer restarts on commit failure ──
static void test_candidate_unknown_retry(void)
{
    TEST("T17: UNKNOWN retry restarts timer after commit failure");
    occ_sm_t sm;
    occ_sm_init(&sm, OCC_UNKNOWN);

    // First attempt reaches VACANT at t=120000
    occ_sm_t c = sm;
    occ_sm_eval(&c, false, 0, 2000, 120000);
    occ_sm_eval(&c, false, 120000, 2000, 120000);
    if (c.state != OCC_VACANT) { FAIL("candidate should reach VACANT"); return; }
    // Commit FAILED

    // Retry: timer restarts, need another exit_delay
    c = sm;
    occ_sm_eval(&c, false, 120500, 2000, 120000);
    if (c.state != OCC_UNKNOWN) { FAIL("should restart UNKNOWN timer"); return; }
    occ_sm_eval(&c, false, 120500 + 120000, 2000, 120000);
    if (c.state != OCC_VACANT) { FAIL("should reach VACANT on retry"); return; }
    PASS();
}

int main(void)
{
    printf("occupancy state machine unit tests\n");
    printf("==================================\n\n");

    test_entry_not_confirmed();
    test_direction_reset();
    test_exit_delay();
    test_exit_cancelled();
    test_unknown_recover_vacant();
    test_unknown_recover_occupied();
    test_unknown_direction_reversal();
    test_unknown_initial_values();
    test_tick_wraparound();
    test_rapid_glitch();
    test_timer_exact_boundary();
    test_exit_exact_boundary();
    test_idle_vacant_stays_vacant();
    test_unknown_from_occupied();
    test_long_idle_vacant();
    test_candidate_restarts_timer();
    test_candidate_unknown_retry();

    printf("\n---\n");
    printf("PASS: %d  FAIL: %d  TOTAL: %d\n",
           s_pass, s_fail, s_pass + s_fail);

    return s_fail > 0 ? 1 : 0;
}
