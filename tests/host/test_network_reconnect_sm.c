// Host unit tests for network_reconnect_sm (pure logic, no FreeRTOS shim).
//
// Coverage map (commissioning-lifecycle.md §3.4, phase 3 §1):
//   T01  init no creds + STA_START(has_saved=false) → no action, stays DISCONNECTED
//   T02  init with creds + STA_START(has_saved=true) → CONNECTING + WIFI_CONNECT
//   T03  CONNECTING + GOT_IP → CONNECTED + STOP_TIMER + NOTIFY_CONNECTED
//   T04  CONNECTING + DISCONNECTED(non-auth) → DISCONNECTED + START_TIMER(1s)
//   T05  full backoff sequence 1→2→4→8→16→32→60→60
//   T06  backoff caps at 60s for attempt > 6
//   T07  auth_fail attempt 1 → DISCONNECTED + START_TIMER
//   T08  auth_fail attempt 2 → DISCONNECTED + START_TIMER
//   T09  auth_fail attempt 3 → STOPPED + NOTIFY_STOPPED (no START_TIMER)
//   T10  STOPPED + DISCONNECTED → ignored (no action)
//   T11  STOPPED + TIMER_FIRED → ignored (stale-timer guard)
//   T12  STOPPED + PROVISIONED → CONNECTING + WIFI_CONNECT (recovery)
//   T13  CONNECTED + TIMER_FIRED → ignored (stale-timer guard)
//   T14  GOT_IP resets reconnect_attempts and auth_fail_attempts to 0
//   T15  PROVISIONED resets auth_fail_attempts (post-recovery full cycle)
//   T16  PROVISIONED during CONNECTING → STOP_TIMER + WIFI_DISCONNECT + WIFI_CONNECT
//   T17  PROVISIONED during CONNECTED → force reconnect cycle
//   T18  PROVISIONED during DISCONNECTED with timer_armed → STOP_TIMER issued
//   T19  DISCONNECTED during STOPPED → ignored
//   T20  GOT_IP during DISCONNECTED → CONNECTED (ESP-Matter owns connect)
//   T21  TIMER_FIRED during CONNECTING → ignored (stale, timer_armed cleared)
//   T22  TIMER_FIRED during STOPPED → ignored (stale, timer_armed cleared)
//   T23  auth_fail then non-auth disconnect keeps auth_fail_attempts
//   T24  PROVISIONED's WIFI_DISCONNECT-induced DISCONNECTED is swallowed
//   T25  After swallow, real AUTH_FAIL still counts toward stop
//   T26  After GOT_IP reset, subsequent disconnect restarts backoff at 1s
//   T27  reason_is_auth_fail classification for all known reasons
//   T28  compute_backoff_ms boundary values (0, 1, 6, 7, 100)
//   T29  STA_START no creds then PROVISIONED connects
//   T30  PROVISIONED clears timer_armed even if no timer was armed

#include <stdio.h>
#include <string.h>

#include "network_reconnect_sm.h"

static int s_pass = 0;
static int s_fail = 0;

#define TEST(name) do { printf("  %-50s ", name); } while (0)
#define PASS()     do { printf("PASS\n"); s_pass++; } while (0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); s_fail++; } while (0)

// ESP-IDF WIFI_REASON_* constants (replicated for host tests).
// Values match ESP-IDF v5.4.1 wifi_err_reason_t.
#define REASON_NO_AP_FOUND            201u
#define REASON_AUTH_FAIL              202u
#define REASON_4WAY_HANDSHAKE_TIMEOUT  15u
#define REASON_HANDSHAKE_TIMEOUT      204u
#define REASON_MIC_FAILURE             14u
#define REASON_802_1X_AUTH_FAILED      23u
#define REASON_ASSOC_LEAVE              8u
#define REASON_BEACON_TIMEOUT         200u

// Helper macros to create event structs as compound literals (C99 compliant).
// These produce lvalues that can have their address taken.
#define EV_STA_START(has_creds) \
    ((net_sm_event_t){ .type = NET_SM_EVENT_STA_START, .has_saved_creds = (has_creds) })

#define EV_DISCONNECT(reason_val) \
    ((net_sm_event_t){ .type = NET_SM_EVENT_DISCONNECTED, .disconnect_reason = (reason_val) })

#define EV_SIMPLE(evt_type) \
    ((net_sm_event_t){ .type = (evt_type) })

#define EV_TIMER_FIRED(gen) \
    ((net_sm_event_t){ .type = NET_SM_EVENT_TIMER_FIRED, .timer_generation = (gen) })

// ── T01 ──
static void test_init_no_creds_sta_start_no_action(void)
{
    TEST("T01: no creds + STA_START → no action");
    net_sm_t sm;
    net_sm_init(&sm, false);
    net_sm_event_t e = EV_STA_START(false);
    net_sm_action_t a;
    bool changed = net_sm_step(&sm, &e, &a);
    if (changed)            { FAIL("unexpected state change"); return; }
    if (a.flags != 0)       { FAIL("expected no actions"); return; }
    if (sm.state != NET_SM_STATE_DISCONNECTED) { FAIL("should stay DISCONNECTED"); return; }
    PASS();
}

// ── T02 ──
static void test_init_with_creds_sta_start_connects(void)
{
    TEST("T02: creds + STA_START → CONNECTING + WIFI_CONNECT");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_event_t e = EV_STA_START(true);
    net_sm_action_t a;
    bool changed = net_sm_step(&sm, &e, &a);
    if (!changed)                                   { FAIL("expected state change"); return; }
    if (sm.state != NET_SM_STATE_CONNECTING)        { FAIL("should be CONNECTING"); return; }
    if (!(a.flags & NET_SM_ACT_WIFI_CONNECT))       { FAIL("missing WIFI_CONNECT"); return; }
    if (a.flags & NET_SM_ACT_START_TIMER)           { FAIL("should not start timer"); return; }
    if (a.flags & NET_SM_ACT_NOTIFY_STATUS)         { FAIL("should not notify on STA_START"); return; }
    PASS();
}

// ── T03 ──
static void test_connecting_got_ip_connected_notifies(void)
{
    TEST("T03: CONNECTING + GOT_IP → CONNECTED + NOTIFY");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    bool changed = net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_GOT_IP), &a);
    if (!changed)                                   { FAIL("expected state change"); return; }
    if (sm.state != NET_SM_STATE_CONNECTED)         { FAIL("should be CONNECTED"); return; }
    if (!(a.flags & NET_SM_ACT_NOTIFY_STATUS))      { FAIL("missing NOTIFY_STATUS"); return; }
    if (a.notify_status != NET_SM_STATUS_CONNECTED) { FAIL("wrong status"); return; }
    if (sm.reconnect_attempts != 0)                 { FAIL("reconnect_attempts not reset"); return; }
    if (sm.auth_fail_attempts != 0)                 { FAIL("auth_fail_attempts not reset"); return; }
    PASS();
}

// ── T04 ──
static void test_connecting_disconnect_non_auth_schedules_1s(void)
{
    TEST("T04: CONNECTING + DISCONNECTED(non-auth) → 1s backoff");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    bool changed = net_sm_step(&sm, &EV_DISCONNECT(REASON_NO_AP_FOUND), &a);
    if (!changed)                                       { FAIL("expected state change"); return; }
    if (sm.state != NET_SM_STATE_DISCONNECTED)          { FAIL("should be DISCONNECTED"); return; }
    if (!(a.flags & NET_SM_ACT_START_TIMER))            { FAIL("missing START_TIMER"); return; }
    if (a.timer_delay_ms != 1000U)                      { FAIL("delay should be 1s"); return; }
    if (!sm.timer_armed)                                { FAIL("timer_armed should be true"); return; }
    if (sm.reconnect_attempts != 1)                     { FAIL("reconnect_attempts should be 1"); return; }
    if (a.flags & NET_SM_ACT_WIFI_CONNECT)              { FAIL("should not WIFI_CONNECT here"); return; }
    if (a.notify_status != NET_SM_STATUS_DISCONNECTED)  { FAIL("wrong notify status"); return; }
    PASS();
}

// ── T05 ──
static void test_full_backoff_sequence(void)
{
    TEST("T05: full backoff 1→2→4→8→16→32→60→60");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);

    uint32_t expected[8] = {1000, 2000, 4000, 8000, 16000, 32000, 60000, 60000};
    for (int i = 0; i < 8; ++i) {
        net_sm_step(&sm, &EV_DISCONNECT(REASON_NO_AP_FOUND), &a);
        if (!(a.flags & NET_SM_ACT_START_TIMER)) {
            printf("\n    attempt %d: missing START_TIMER\n", i + 1);
            FAIL("missing START_TIMER"); return;
        }
        if (a.timer_delay_ms != expected[i]) {
            printf("\n    attempt %d: delay=%u, expected=%u\n",
                   i + 1, a.timer_delay_ms, expected[i]);
            FAIL("wrong delay"); return;
        }
        // TIMER_FIRED → CONNECTING (re-attempt)
        net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
        if (sm.state != NET_SM_STATE_CONNECTING) {
            printf("\n    attempt %d: not CONNECTING after TIMER_FIRED\n", i + 1);
            FAIL("not CONNECTING"); return;
        }
    }
    PASS();
}

// ── T06 ──
static void test_backoff_caps_at_60s(void)
{
    TEST("T06: backoff caps at 60s for attempt 7+");
    if (net_sm_compute_backoff_ms(0) != 0)        { FAIL("attempt 0 should be 0"); return; }
    if (net_sm_compute_backoff_ms(7) != 60000U)   { FAIL("attempt 7 should be 60000"); return; }
    if (net_sm_compute_backoff_ms(50) != 60000U)  { FAIL("attempt 50 should be 60000"); return; }
    if (net_sm_compute_backoff_ms(255) != 60000U) { FAIL("attempt 255 should be 60000"); return; }
    PASS();
}

// ── T07/T08/T09: auth_fail sequence ──
static void test_auth_fail_sequence_to_stop(void)
{
    TEST("T07-T09: auth_fail 1,2 → backoff; 3 → STOPPED");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);

    // Attempt 1
    net_sm_step(&sm, &EV_DISCONNECT(REASON_AUTH_FAIL), &a);
    if (sm.state != NET_SM_STATE_DISCONNECTED)          { FAIL("T07: should be DISCONNECTED"); return; }
    if (sm.auth_fail_attempts != 1)                     { FAIL("T07: auth_fail_attempts should be 1"); return; }
    if (!(a.flags & NET_SM_ACT_START_TIMER))            { FAIL("T07: missing START_TIMER"); return; }
    if (a.timer_delay_ms != 1000U)                      { FAIL("T07: delay should be 1s"); return; }

    // Timer fires, attempt 2
    net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    net_sm_step(&sm, &EV_DISCONNECT(REASON_AUTH_FAIL), &a);
    if (sm.auth_fail_attempts != 2)                     { FAIL("T08: auth_fail_attempts should be 2"); return; }
    if (!(a.flags & NET_SM_ACT_START_TIMER))            { FAIL("T08: missing START_TIMER"); return; }
    if (a.timer_delay_ms != 2000U)                      { FAIL("T08: delay should be 2s"); return; }

    // Timer fires, attempt 3 → STOPPED
    net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    bool changed = net_sm_step(&sm, &EV_DISCONNECT(REASON_AUTH_FAIL), &a);
    if (!changed)                                       { FAIL("T09: expected state change to STOPPED"); return; }
    if (sm.state != NET_SM_STATE_STOPPED)               { FAIL("T09: should be STOPPED"); return; }
    if (sm.auth_fail_attempts != 3)                     { FAIL("T09: auth_fail_attempts should be 3"); return; }
    if (a.flags & NET_SM_ACT_START_TIMER)               { FAIL("T09: should NOT start timer in STOPPED"); return; }
    if (a.notify_status != NET_SM_STATUS_STOPPED)       { FAIL("T09: should notify STOPPED"); return; }
    PASS();
}

// ── T10 ──
static void test_stopped_disconnect_ignored(void)
{
    TEST("T10: STOPPED + DISCONNECTED → ignored");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    // Burn 3 auth fails to reach STOPPED.
    for (int i = 0; i < 3; ++i) {
        net_sm_step(&sm, &EV_DISCONNECT(REASON_AUTH_FAIL), &a);
        if (i < 2) net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    }
    if (sm.state != NET_SM_STATE_STOPPED) { FAIL("precondition: should be STOPPED"); return; }

    bool changed = net_sm_step(&sm, &EV_DISCONNECT(REASON_NO_AP_FOUND), &a);
    if (changed)            { FAIL("should not change state"); return; }
    if (a.flags != 0)       { FAIL("should issue no actions"); return; }
    PASS();
}

// ── T11 ──
static void test_stopped_timer_fired_ignored(void)
{
    TEST("T11: STOPPED + TIMER_FIRED → ignored");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    for (int i = 0; i < 3; ++i) {
        net_sm_step(&sm, &EV_DISCONNECT(REASON_AUTH_FAIL), &a);
        if (i < 2) net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    }
    // Simulate a stray timer fire (shouldn't happen since no START_TIMER in STOPPED,
    // but a stale timer could fire if it was armed before transitioning to STOPPED).
    sm.timer_armed = true;  // force stale-armed state
    bool changed = net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    if (changed)                  { FAIL("should not change state"); return; }
    if (a.flags != 0)             { FAIL("should issue no actions"); return; }
    if (sm.timer_armed)           { FAIL("timer_armed should be cleared"); return; }
    PASS();
}

// ── T12 ──
static void test_stopped_provisioned_recovers(void)
{
    TEST("T12: STOPPED + PROVISIONED → CONNECTING (recovery)");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    for (int i = 0; i < 3; ++i) {
        net_sm_step(&sm, &EV_DISCONNECT(REASON_AUTH_FAIL), &a);
        if (i < 2) net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    }
    if (sm.state != NET_SM_STATE_STOPPED) { FAIL("precondition: should be STOPPED"); return; }

    bool changed = net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_PROVISIONED), &a);
    if (!changed)                                       { FAIL("expected state change"); return; }
    if (sm.state != NET_SM_STATE_CONNECTING)            { FAIL("should be CONNECTING"); return; }
    if (sm.auth_fail_attempts != 0)                     { FAIL("auth_fail_attempts should be reset"); return; }
    if (sm.reconnect_attempts != 0)                     { FAIL("reconnect_attempts should be reset"); return; }
    if (!(a.flags & NET_SM_ACT_WIFI_CONNECT))           { FAIL("missing WIFI_CONNECT"); return; }
    if (a.flags & NET_SM_ACT_WIFI_DISCONNECT)           { FAIL("should NOT WIFI_DISCONNECT from STOPPED"); return; }
    if (!(a.flags & NET_SM_ACT_NOTIFY_STATUS))          { FAIL("missing NOTIFY_STATUS"); return; }
    if (a.notify_status != NET_SM_STATUS_PROVISIONED)   { FAIL("wrong status"); return; }
    PASS();
}

// ── T13 ──
static void test_connected_stale_timer_fired_ignored(void)
{
    TEST("T13: CONNECTED + TIMER_FIRED → ignored");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_GOT_IP), &a);
    if (sm.state != NET_SM_STATE_CONNECTED) { FAIL("precondition: should be CONNECTED"); return; }

    // Simulate a stale timer fire (timer was armed before GOT_IP but fire
    // arrived after — the STOP_TIMER action should have been issued by GOT_IP
    // but the timer may already be in the timer-service queue).
    sm.timer_armed = true;
    bool changed = net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    if (changed)            { FAIL("should not change state"); return; }
    if (a.flags != 0)       { FAIL("should issue no actions"); return; }
    if (sm.timer_armed)     { FAIL("timer_armed should be cleared"); return; }
    PASS();
}

// ── T14 ──
static void test_got_ip_resets_counters(void)
{
    TEST("T14: GOT_IP resets reconnect_attempts and auth_fail_attempts");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    // Two failures (non-auth and auth).
    net_sm_step(&sm, &EV_DISCONNECT(REASON_NO_AP_FOUND), &a);   // reconnect_attempts=1
    net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    net_sm_step(&sm, &EV_DISCONNECT(REASON_AUTH_FAIL), &a);     // reconnect_attempts=2, auth=1
    net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    if (sm.reconnect_attempts != 2) { FAIL("precondition: reconnect_attempts=2"); return; }
    if (sm.auth_fail_attempts != 1) { FAIL("precondition: auth_fail_attempts=1"); return; }

    net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_GOT_IP), &a);
    if (sm.reconnect_attempts != 0) { FAIL("reconnect_attempts should be 0"); return; }
    if (sm.auth_fail_attempts != 0) { FAIL("auth_fail_attempts should be 0"); return; }
    PASS();
}

// ── T15 ──
static void test_provisioned_resets_auth_fail_count(void)
{
    TEST("T15: PROVISIONED resets auth_fail_attempts post-recovery");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    // Two auth failures (not yet stopped).
    net_sm_step(&sm, &EV_DISCONNECT(REASON_AUTH_FAIL), &a);
    net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    net_sm_step(&sm, &EV_DISCONNECT(REASON_AUTH_FAIL), &a);
    if (sm.auth_fail_attempts != 2) { FAIL("precondition: auth_fail_attempts=2"); return; }

    net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_PROVISIONED), &a);
    if (sm.auth_fail_attempts != 0) { FAIL("auth_fail_attempts should be 0"); return; }
    if (sm.reconnect_attempts != 0) { FAIL("reconnect_attempts should be 0"); return; }
    PASS();
}

// ── T16 ──
static void test_provisioned_during_connecting_enters_reconfiguring(void)
{
    TEST("T16: PROVISIONED during CONNECTING → RECONFIGURING");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);  // → CONNECTING
    bool changed = net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_PROVISIONED), &a);
    if (!changed)                                       { FAIL("expected actions"); return; }
    if (sm.state != NET_SM_STATE_RECONFIGURING)         { FAIL("should be RECONFIGURING"); return; }
    if (!(a.flags & NET_SM_ACT_WIFI_DISCONNECT))        { FAIL("missing WIFI_DISCONNECT"); return; }
    if (a.flags & NET_SM_ACT_WIFI_CONNECT)              { FAIL("should NOT WIFI_CONNECT yet"); return; }
    if (!(a.flags & NET_SM_ACT_NOTIFY_STATUS))          { FAIL("missing NOTIFY_STATUS"); return; }
    if (a.notify_status != NET_SM_STATUS_PROVISIONED)   { FAIL("wrong status"); return; }
    PASS();
}

// ── T17 ──
static void test_provisioned_during_connected_enters_reconfiguring(void)
{
    TEST("T17: PROVISIONED during CONNECTED → RECONFIGURING");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_GOT_IP), &a);  // → CONNECTED
    bool changed = net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_PROVISIONED), &a);
    if (!changed)                                       { FAIL("expected actions"); return; }
    if (sm.state != NET_SM_STATE_RECONFIGURING)         { FAIL("should be RECONFIGURING"); return; }
    if (!(a.flags & NET_SM_ACT_WIFI_DISCONNECT))        { FAIL("missing WIFI_DISCONNECT"); return; }
    if (a.flags & NET_SM_ACT_WIFI_CONNECT)              { FAIL("should NOT WIFI_CONNECT yet"); return; }
    PASS();
}

// ── T18 ──
static void test_provisioned_during_disconnected_cancels_timer(void)
{
    TEST("T18: PROVISIONED during DISCONNECTED(timer armed) → STOP_TIMER");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    net_sm_step(&sm, &EV_DISCONNECT(REASON_NO_AP_FOUND), &a);  // arms timer
    if (!sm.timer_armed) { FAIL("precondition: timer should be armed"); return; }

    bool changed = net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_PROVISIONED), &a);
    if (!changed)                                   { FAIL("expected state change"); return; }
    if (sm.state != NET_SM_STATE_CONNECTING)        { FAIL("should be CONNECTING"); return; }
    if (!(a.flags & NET_SM_ACT_STOP_TIMER))         { FAIL("missing STOP_TIMER"); return; }
    if (sm.timer_armed)                             { FAIL("timer_armed should be cleared"); return; }
    PASS();
}

// ── T19 ──
static void test_disconnect_during_stopped_ignored(void)
{
    TEST("T19: DISCONNECTED during STOPPED → ignored");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    for (int i = 0; i < 3; ++i) {
        net_sm_step(&sm, &EV_DISCONNECT(REASON_AUTH_FAIL), &a);
        if (i < 2) net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    }
    if (sm.state != NET_SM_STATE_STOPPED) { FAIL("precondition: should be STOPPED"); return; }

    bool changed = net_sm_step(&sm, &EV_DISCONNECT(REASON_BEACON_TIMEOUT), &a);
    if (changed)            { FAIL("should not change state"); return; }
    if (a.flags != 0)       { FAIL("should issue no actions"); return; }
    PASS();
}

// ── T20 ──
static void test_got_ip_during_disconnected_connects(void)
{
    TEST("T20: GOT_IP during DISCONNECTED → CONNECTED (ESP-Matter owns connect)");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    net_sm_step(&sm, &EV_DISCONNECT(REASON_NO_AP_FOUND), &a);  // → DISCONNECTED
    bool changed = net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_GOT_IP), &a);
    if (!changed)                                   { FAIL("expected state change"); return; }
    if (sm.state != NET_SM_STATE_CONNECTED)         { FAIL("should be CONNECTED"); return; }
    if (!(a.flags & NET_SM_ACT_NOTIFY_STATUS))      { FAIL("missing NOTIFY_STATUS"); return; }
    if (a.notify_status != NET_SM_STATUS_CONNECTED) { FAIL("wrong status"); return; }
    if (sm.reconnect_attempts != 0)                 { FAIL("reconnect_attempts not reset"); return; }
    PASS();
}

// ── T21 ──
static void test_timer_fired_during_connecting_ignored(void)
{
    TEST("T21: TIMER_FIRED during CONNECTING → ignored (stale)");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);  // → CONNECTING
    // Stale timer fire (timer was armed before STA_START in some race).
    sm.timer_armed = true;
    bool changed = net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    if (changed)            { FAIL("should not change state"); return; }
    if (a.flags != 0)       { FAIL("should issue no actions"); return; }
    if (sm.timer_armed)     { FAIL("timer_armed should be cleared"); return; }
    PASS();
}

// ── T22 ──
static void test_timer_fired_during_stopped_ignored(void)
{
    TEST("T22: TIMER_FIRED during STOPPED → ignored (stale)");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    for (int i = 0; i < 3; ++i) {
        net_sm_step(&sm, &EV_DISCONNECT(REASON_AUTH_FAIL), &a);
        if (i < 2) net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    }
    if (sm.state != NET_SM_STATE_STOPPED) { FAIL("precondition: should be STOPPED"); return; }

    sm.timer_armed = true;  // force stale-armed state
    bool changed = net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    if (changed)            { FAIL("should not change state"); return; }
    if (a.flags != 0)       { FAIL("should issue no actions"); return; }
    if (sm.timer_armed)     { FAIL("timer_armed should be cleared"); return; }
    PASS();
}

// ── T23 ──
static void test_auth_fail_then_non_auth_keeps_auth_count(void)
{
    TEST("T23: auth_fail then non-auth keeps auth_fail_attempts");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    net_sm_step(&sm, &EV_DISCONNECT(REASON_AUTH_FAIL), &a);     // auth=1
    net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    net_sm_step(&sm, &EV_DISCONNECT(REASON_NO_AP_FOUND), &a);   // non-auth
    if (sm.auth_fail_attempts != 1) { FAIL("auth_fail_attempts should still be 1"); return; }
    if (sm.reconnect_attempts != 2) { FAIL("reconnect_attempts should be 2"); return; }
    PASS();
}

// ── T24 ──
static void test_reconfiguring_disconnect_triggers_connect(void)
{
    TEST("T24: RECONFIGURING + DISCONNECTED → CONNECTING + WIFI_CONNECT");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_PROVISIONED), &a);
    if (sm.state != NET_SM_STATE_RECONFIGURING) { FAIL("precondition: should be RECONFIGURING"); return; }

    // Self-induced DISCONNECTED (ASSOC_LEAVE from esp_wifi_disconnect).
    bool changed = net_sm_step(&sm, &EV_DISCONNECT(REASON_ASSOC_LEAVE), &a);
    if (!changed)                                   { FAIL("expected state change"); return; }
    if (sm.state != NET_SM_STATE_CONNECTING)        { FAIL("should be CONNECTING"); return; }
    if (!(a.flags & NET_SM_ACT_WIFI_CONNECT))       { FAIL("missing WIFI_CONNECT"); return; }
    if (sm.reconnect_attempts != 0)                 { FAIL("reconnect_attempts should be 0"); return; }
    if (sm.auth_fail_attempts != 0)                 { FAIL("auth_fail_attempts should be 0"); return; }
    PASS();
}

// ── T25 ──
static void test_reconfiguring_then_real_auth_fail_counts(void)
{
    TEST("T25: after RECONFIGURING, real AUTH_FAIL still counts toward stop");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_PROVISIONED), &a);
    // Self-induced DISCONNECTED transitions RECONFIGURING → CONNECTING.
    net_sm_step(&sm, &EV_DISCONNECT(REASON_ASSOC_LEAVE), &a);
    if (sm.state != NET_SM_STATE_CONNECTING) { FAIL("precondition: should be CONNECTING"); return; }

    // Real auth failure (bad new passphrase).
    net_sm_step(&sm, &EV_DISCONNECT(REASON_AUTH_FAIL), &a);
    if (sm.auth_fail_attempts != 1) { FAIL("auth_fail_attempts should be 1"); return; }
    if (sm.reconnect_attempts != 1) { FAIL("reconnect_attempts should be 1"); return; }
    if (sm.state != NET_SM_STATE_DISCONNECTED) { FAIL("should be DISCONNECTED"); return; }
    if (!(a.flags & NET_SM_ACT_START_TIMER))   { FAIL("missing START_TIMER"); return; }
    PASS();
}

// ── T26 ──
static void test_backoff_resets_after_got_ip(void)
{
    TEST("T26: after GOT_IP reset, next disconnect restarts at 1s");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    // Burn 3 failures to push backoff to 4s.
    net_sm_step(&sm, &EV_DISCONNECT(REASON_NO_AP_FOUND), &a);   // 1s
    net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    net_sm_step(&sm, &EV_DISCONNECT(REASON_NO_AP_FOUND), &a);   // 2s
    net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    net_sm_step(&sm, &EV_DISCONNECT(REASON_NO_AP_FOUND), &a);   // 4s
    if (sm.reconnect_attempts != 3) { FAIL("precondition: reconnect_attempts=3"); return; }

    // Eventually connect.
    net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_GOT_IP), &a);
    if (sm.reconnect_attempts != 0) { FAIL("GOT_IP should reset"); return; }

    // Disconnect again — should restart at 1s.
    net_sm_step(&sm, &EV_DISCONNECT(REASON_BEACON_TIMEOUT), &a);
    if (sm.reconnect_attempts != 1)         { FAIL("reconnect_attempts should be 1"); return; }
    if (a.timer_delay_ms != 1000U)          { FAIL("delay should be 1s after reset"); return; }
    PASS();
}

// ── T27 ──
static void test_reason_is_auth_fail_classification(void)
{
    TEST("T27: reason_is_auth_fail classification");
    if (!net_sm_reason_is_auth_fail(REASON_AUTH_FAIL))                { FAIL("AUTH_FAIL"); return; }
    if (!net_sm_reason_is_auth_fail(REASON_4WAY_HANDSHAKE_TIMEOUT))   { FAIL("4WAY_HANDSHAKE_TIMEOUT"); return; }
    if (!net_sm_reason_is_auth_fail(REASON_HANDSHAKE_TIMEOUT))        { FAIL("HANDSHAKE_TIMEOUT"); return; }
    if (!net_sm_reason_is_auth_fail(REASON_MIC_FAILURE))              { FAIL("MIC_FAILURE"); return; }
    if (!net_sm_reason_is_auth_fail(REASON_802_1X_AUTH_FAILED))       { FAIL("802_1X_AUTH_FAILED"); return; }
    if (net_sm_reason_is_auth_fail(REASON_NO_AP_FOUND))               { FAIL("NO_AP_FOUND should be false"); return; }
    if (net_sm_reason_is_auth_fail(REASON_BEACON_TIMEOUT))            { FAIL("BEACON_TIMEOUT should be false"); return; }
    if (net_sm_reason_is_auth_fail(REASON_ASSOC_LEAVE))               { FAIL("ASSOC_LEAVE should be false"); return; }
    PASS();
}

// ── T28 ──
static void test_compute_backoff_ms_boundaries(void)
{
    TEST("T28: compute_backoff_ms boundary values");
    if (net_sm_compute_backoff_ms(0)   != 0)        { FAIL("attempt 0 should be 0"); return; }
    if (net_sm_compute_backoff_ms(1)   != 1000U)    { FAIL("attempt 1 should be 1000"); return; }
    if (net_sm_compute_backoff_ms(2)   != 2000U)    { FAIL("attempt 2 should be 2000"); return; }
    if (net_sm_compute_backoff_ms(3)   != 4000U)    { FAIL("attempt 3 should be 4000"); return; }
    if (net_sm_compute_backoff_ms(4)   != 8000U)    { FAIL("attempt 4 should be 8000"); return; }
    if (net_sm_compute_backoff_ms(5)   != 16000U)   { FAIL("attempt 5 should be 16000"); return; }
    if (net_sm_compute_backoff_ms(6)   != 32000U)   { FAIL("attempt 6 should be 32000"); return; }
    if (net_sm_compute_backoff_ms(7)   != 60000U)   { FAIL("attempt 7 should be 60000"); return; }
    if (net_sm_compute_backoff_ms(100) != 60000U)   { FAIL("attempt 100 should be 60000"); return; }
    PASS();
}

// ── T29 ──
static void test_sta_start_no_creds_then_provisioned_connects(void)
{
    TEST("T29: STA_START(no creds) then PROVISIONED connects");
    net_sm_t sm;
    net_sm_init(&sm, false);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(false), &a);  // no action
    if (sm.state != NET_SM_STATE_DISCONNECTED) { FAIL("should be DISCONNECTED"); return; }

    bool changed = net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_PROVISIONED), &a);
    if (!changed)                                   { FAIL("expected state change"); return; }
    if (sm.state != NET_SM_STATE_CONNECTING)        { FAIL("should be CONNECTING"); return; }
    if (!sm.provisioned)                            { FAIL("provisioned should be true"); return; }
    if (!(a.flags & NET_SM_ACT_WIFI_CONNECT))       { FAIL("missing WIFI_CONNECT"); return; }
    PASS();
}

// ── T30 ──
static void test_provisioned_clears_timer_armed_even_if_not_armed(void)
{
    TEST("T30: PROVISIONED clears timer_armed flag (no STOP_TIMER if not armed)");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);  // → CONNECTING, timer_armed=false
    bool changed = net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_PROVISIONED), &a);
    if (!changed)                            { FAIL("expected state change"); return; }
    if (a.flags & NET_SM_ACT_STOP_TIMER)     { FAIL("should NOT issue STOP_TIMER when timer not armed"); return; }
    if (sm.timer_armed)                      { FAIL("timer_armed should be false"); return; }
    PASS();
}

// ── T31 ──
static void test_action_fail_wifi_connect_returns_to_disconnected(void)
{
    TEST("T31: WIFI_CONNECT failure in CONNECTING → DISCONNECTED + backoff");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);  // → CONNECTING
    if (sm.state != NET_SM_STATE_CONNECTING) { FAIL("precondition"); return; }

    // Simulate action failure feedback.
    net_sm_event_t fail_ev = {
        .type      = NET_SM_EVENT_ACTION_FAILED,
        .fail_type = NET_SM_FAIL_WIFI_CONNECT,
    };
    bool changed = net_sm_step(&sm, &fail_ev, &a);
    if (!changed)                                    { FAIL("expected state change"); return; }
    if (sm.state != NET_SM_STATE_DISCONNECTED)       { FAIL("should be DISCONNECTED"); return; }
    if (!(a.flags & NET_SM_ACT_START_TIMER))         { FAIL("missing START_TIMER"); return; }
    if (a.timer_delay_ms != 1000U)                   { FAIL("delay should be 1s"); return; }
    PASS();
}

// ── T32 ──
static void test_action_fail_wifi_disconnect_skips_to_connect(void)
{
    TEST("T32: WIFI_DISCONNECT failure in RECONFIGURING → CONNECTING");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_PROVISIONED), &a);
    if (sm.state != NET_SM_STATE_RECONFIGURING) { FAIL("precondition"); return; }

    // Simulate WIFI_DISCONNECT failure.
    net_sm_event_t fail_ev = {
        .type      = NET_SM_EVENT_ACTION_FAILED,
        .fail_type = NET_SM_FAIL_WIFI_DISCONNECT,
    };
    bool changed = net_sm_step(&sm, &fail_ev, &a);
    if (!changed)                                    { FAIL("expected actions"); return; }
    if (sm.state != NET_SM_STATE_CONNECTING)         { FAIL("should be CONNECTING"); return; }
    if (!(a.flags & NET_SM_ACT_WIFI_CONNECT))        { FAIL("missing WIFI_CONNECT"); return; }
    PASS();
}

// ── T33 ──
static void test_first_commissioning_no_self_disconnect(void)
{
    TEST("T33: first commissioning (DISCONNECTED) → CONNECTING directly");
    net_sm_t sm;
    net_sm_init(&sm, false);   // no saved creds
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(false), &a);  // no action
    if (sm.state != NET_SM_STATE_DISCONNECTED) { FAIL("precondition"); return; }

    // PROVISIONED from DISCONNECTED → should go directly to CONNECTING.
    bool changed = net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_PROVISIONED), &a);
    if (!changed)                                    { FAIL("expected actions"); return; }
    if (sm.state != NET_SM_STATE_CONNECTING)         { FAIL("should be CONNECTING"); return; }
    if (!(a.flags & NET_SM_ACT_WIFI_CONNECT))        { FAIL("missing WIFI_CONNECT"); return; }
    if (a.flags & NET_SM_ACT_WIFI_DISCONNECT)        { FAIL("should NOT WIFI_DISCONNECT"); return; }
    if (a.notify_status != NET_SM_STATUS_PROVISIONED){ FAIL("wrong status"); return; }
    PASS();
}

// ── T34 ──
static void test_provisioned_during_disconnected_no_reconfiguring(void)
{
    TEST("T34: PROVISIONED during DISCONNECTED → CONNECTING (no RECONFIGURING)");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);  // → CONNECTING
    net_sm_step(&sm, &EV_DISCONNECT(REASON_NO_AP_FOUND), &a);  // → DISCONNECTED
    if (sm.state != NET_SM_STATE_DISCONNECTED) { FAIL("precondition"); return; }

    bool changed = net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_PROVISIONED), &a);
    if (!changed)                                    { FAIL("expected actions"); return; }
    if (sm.state != NET_SM_STATE_CONNECTING)         { FAIL("should be CONNECTING"); return; }
    if (!(a.flags & NET_SM_ACT_WIFI_CONNECT))        { FAIL("missing WIFI_CONNECT"); return; }
    if (a.flags & NET_SM_ACT_WIFI_DISCONNECT)        { FAIL("should NOT WIFI_DISCONNECT"); return; }
    PASS();
}

// ── T35 ──
static void test_got_ip_in_reconfiguring_connects(void)
{
    TEST("T35: GOT_IP during RECONFIGURING → CONNECTED (ESP-Matter owns connect)");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_PROVISIONED), &a);
    if (sm.state != NET_SM_STATE_RECONFIGURING) { FAIL("precondition"); return; }

    bool changed = net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_GOT_IP), &a);
    if (!changed)                                   { FAIL("expected state change"); return; }
    if (sm.state != NET_SM_STATE_CONNECTED)         { FAIL("should be CONNECTED"); return; }
    if (!(a.flags & NET_SM_ACT_NOTIFY_STATUS))      { FAIL("missing NOTIFY_STATUS"); return; }
    if (a.notify_status != NET_SM_STATUS_CONNECTED) { FAIL("wrong status"); return; }
    PASS();
}

// ── T36 ──
static void test_reconfiguring_stale_timer_fired_ignored(void)
{
    TEST("T36: TIMER_FIRED during RECONFIGURING → ignored");
    net_sm_t sm;
    net_sm_init(&sm, true);
    net_sm_action_t a;
    net_sm_step(&sm, &EV_STA_START(true), &a);
    net_sm_step(&sm, &EV_SIMPLE(NET_SM_EVENT_PROVISIONED), &a);
    if (sm.state != NET_SM_STATE_RECONFIGURING) { FAIL("precondition"); return; }

    sm.timer_armed = true;  // stale
    bool changed = net_sm_step(&sm, &EV_TIMER_FIRED(sm.timer_generation), &a);
    if (changed)            { FAIL("should not change state"); return; }
    if (a.flags != 0)       { FAIL("should issue no actions"); return; }
    if (sm.timer_armed)     { FAIL("timer_armed should be cleared"); return; }
    PASS();
}

int main(void)
{
    printf("network_reconnect_sm unit tests\n");
    printf("================================\n\n");

    test_init_no_creds_sta_start_no_action();
    test_init_with_creds_sta_start_connects();
    test_connecting_got_ip_connected_notifies();
    test_connecting_disconnect_non_auth_schedules_1s();
    test_full_backoff_sequence();
    test_backoff_caps_at_60s();
    test_auth_fail_sequence_to_stop();
    test_stopped_disconnect_ignored();
    test_stopped_timer_fired_ignored();
    test_stopped_provisioned_recovers();
    test_connected_stale_timer_fired_ignored();
    test_got_ip_resets_counters();
    test_provisioned_resets_auth_fail_count();
    test_provisioned_during_connecting_enters_reconfiguring();
    test_provisioned_during_connected_enters_reconfiguring();
    test_provisioned_during_disconnected_cancels_timer();
    test_disconnect_during_stopped_ignored();
    test_got_ip_during_disconnected_connects();
    test_timer_fired_during_connecting_ignored();
    test_timer_fired_during_stopped_ignored();
    test_auth_fail_then_non_auth_keeps_auth_count();
    test_reconfiguring_disconnect_triggers_connect();
    test_reconfiguring_then_real_auth_fail_counts();
    test_backoff_resets_after_got_ip();
    test_reason_is_auth_fail_classification();
    test_compute_backoff_ms_boundaries();
    test_sta_start_no_creds_then_provisioned_connects();
    test_provisioned_clears_timer_armed_even_if_not_armed();
    test_action_fail_wifi_connect_returns_to_disconnected();
    test_action_fail_wifi_disconnect_skips_to_connect();
    test_first_commissioning_no_self_disconnect();
    test_provisioned_during_disconnected_no_reconfiguring();
    test_got_ip_in_reconfiguring_connects();
    test_reconfiguring_stale_timer_fired_ignored();

    printf("\n---\n");
    printf("PASS: %d  FAIL: %d  TOTAL: %d\n",
           s_pass, s_fail, s_pass + s_fail);
    return s_fail > 0 ? 1 : 0;
}
