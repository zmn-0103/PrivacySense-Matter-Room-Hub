#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_rgb.h"

static int s_pass = 0;
static int s_fail = 0;

#define TEST(name) do { printf("  %-50s ", name); } while (0)
#define PASS()     do { printf("PASS\n"); s_pass++; } while (0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); s_fail++; } while (0)

static void fill_state(room_state_t *st)
{
    memset(st, 0, sizeof(*st));
    st->occupancy          = OCCUPANCY_VACANT;
    st->user_mode          = MODE_NORMAL;
    st->pre_night_mode     = MODE_NORMAL;
    st->env_alert          = ALERT_OK;
    st->wifi_connected     = false;
    st->matter_commissioned = false;
    st->commissioning_active = false;
    st->radar_online       = true;
    st->env_sensor_online  = true;
}

// ── T01: P1 sensor fail (radar offline) → red blink ──
static void test_p1_sensor_fail_radar(void)
{
    TEST("T01: P1 radar offline -> red blink");
    room_state_t st;
    fill_state(&st);
    st.radar_online = false;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.priority != 1)          { FAIL("priority should be 1"); return; }
    if (out.pattern != RGB_PATTERN_RED_BLINK) { FAIL("pattern RED_BLINK"); return; }
    if (out.r == 0)                 { FAIL("should be red (r > 0)"); return; }
    if (out.g != 0 || out.b != 0)   { FAIL("only red channel"); return; }

    ui_rgb_compute(&st, 500, &out);
    if (out.r != 0)                 { FAIL("should be off at t=500"); return; }
    if (out.pattern != RGB_PATTERN_RED_BLINK) { FAIL("pattern still RED_BLINK"); return; }
    PASS();
}

// ── T02: P1 env offline → yellow blip at t=0 ──
static void test_p1_env_offline_yellow_blip(void)
{
    TEST("T02: P1 env offline -> yellow blip at t=0");
    room_state_t st;
    fill_state(&st);
    st.radar_online = true;
    st.env_sensor_online = false;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.priority != 1)                     { FAIL("priority 1"); return; }
    if (out.pattern != RGB_PATTERN_RED_YELLOW_BLIP) { FAIL("pattern RED_YELLOW_BLIP"); return; }
    if (out.r != 63 || out.g != 63)            { FAIL("yellow blip at t=0"); return; }

    ui_rgb_compute(&st, 100, &out);
    if (out.pattern != RGB_PATTERN_RED_YELLOW_BLIP) { FAIL("pattern RED_YELLOW_BLIP"); return; }
    if (out.r != 63 || out.g != 63)            { FAIL("yellow blip at t=100"); return; }

    ui_rgb_compute(&st, 199, &out);
    if (out.pattern != RGB_PATTERN_RED_YELLOW_BLIP) { FAIL("pattern RED_YELLOW_BLIP at 199"); return; }
    if (out.r != 63 || out.g != 63)            { FAIL("yellow blip at t=199"); return; }

    ui_rgb_compute(&st, 200, &out);
    if (out.pattern != RGB_PATTERN_RED_BLINK)  { FAIL("pattern RED_BLINK after window"); return; }
    if (out.r != 64 || out.g != 0)             { FAIL("red blink at t=200"); return; }
    PASS();
}

// ── T03: P2 commissioning active → blue fast blink ──
static void test_p2_commissioning(void)
{
    TEST("T03: P2 commissioning active -> blue blink");
    room_state_t st;
    fill_state(&st);
    st.commissioning_active = true;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.priority != 2)                       { FAIL("priority 2"); return; }
    if (out.pattern != RGB_PATTERN_BLUE_FAST_BLINK) { FAIL("pattern BLUE_FAST_BLINK"); return; }
    if (out.b != 32)                             { FAIL("should be blue at t=0"); return; }
    if (out.r != 0 || out.g != 0)                { FAIL("only blue channel"); return; }

    ui_rgb_compute(&st, 250, &out);
    if (out.pattern != RGB_PATTERN_BLUE_FAST_BLINK) { FAIL("pattern still BLUE_FAST_BLINK"); return; }
    if (out.b != 0)                              { FAIL("should be off at t=250"); return; }

    ui_rgb_compute(&st, 500, &out);
    if (out.pattern != RGB_PATTERN_BLUE_FAST_BLINK) { FAIL("pattern still BLUE_FAST_BLINK"); return; }
    if (out.b != 32)                             { FAIL("should be blue at t=500"); return; }
    PASS();
}

// ── T04: P2 not commissioned but not active → falls through ──
static void test_p2_not_commissioning(void)
{
    TEST("T04: matter not commissioned + !active -> P3 or lower");
    room_state_t st;
    fill_state(&st);
    st.commissioning_active = false;
    st.wifi_connected       = true;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.priority == 2)    { FAIL("should NOT show P2"); return; }
    if (out.r != 0 || out.g != 0 || out.b != 0) { FAIL("should be off"); return; }
    if (out.pattern != RGB_PATTERN_OFF) { FAIL("pattern OFF"); return; }
    PASS();
}

// ── T05: P3 wifi down → white slow blink ──
static void test_p3_wifi_down(void)
{
    TEST("T05: P3 wifi disconnected -> white blink");
    room_state_t st;
    fill_state(&st);
    st.wifi_connected = false;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.priority != 3)                       { FAIL("priority 3"); return; }
    if (out.pattern != RGB_PATTERN_WHITE_SLOW_BLINK) { FAIL("pattern WHITE_SLOW_BLINK"); return; }
    if (out.r != 16 || out.g != 16 || out.b != 16) { FAIL("white at t=0"); return; }

    ui_rgb_compute(&st, 1000, &out);
    if (out.pattern != RGB_PATTERN_WHITE_SLOW_BLINK) { FAIL("pattern still WHITE_SLOW_BLINK"); return; }
    if (out.r != 0)                              { FAIL("off at t=1000"); return; }
    PASS();
}

// ── T06: P4 env alert → yellow steady ──
static void test_p4_env_alert(void)
{
    TEST("T06: P4 env alert -> yellow steady");
    room_state_t st;
    fill_state(&st);
    st.wifi_connected = true;
    st.env_alert      = ALERT_ACTIVE;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.priority != 4)             { FAIL("priority 4"); return; }
    if (out.pattern != RGB_PATTERN_YELLOW_STEADY) { FAIL("pattern YELLOW_STEADY"); return; }
    if (out.r != 127 || out.g != 127)  { FAIL("yellow steady"); return; }

    ui_rgb_compute(&st, 5000, &out);
    if (out.pattern != RGB_PATTERN_YELLOW_STEADY) { FAIL("pattern still YELLOW_STEADY"); return; }
    if (out.r != 127)                  { FAIL("still yellow at 5s"); return; }
    PASS();
}

// ── T07: P5 occupied + NIGHT → warm white ──
static void test_p5_occupied_night(void)
{
    TEST("T07: P5 occupied + NIGHT -> warm white");
    room_state_t st;
    fill_state(&st);
    st.wifi_connected = true;
    st.occupancy      = OCCUPANCY_OCCUPIED;
    st.user_mode      = MODE_NIGHT;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.priority != 5)             { FAIL("priority 5"); return; }
    if (out.pattern != RGB_PATTERN_WARM_WHITE) { FAIL("pattern WARM_WHITE"); return; }
    if (out.r != 25 || out.g != 18 || out.b != 5) { FAIL("warm white"); return; }
    PASS();
}

// ── T08: P6 occupied + QUIET → blue low ──
static void test_p6_occupied_quiet(void)
{
    TEST("T08: P6 occupied + QUIET -> blue low");
    room_state_t st;
    fill_state(&st);
    st.wifi_connected = true;
    st.occupancy      = OCCUPANCY_OCCUPIED;
    st.user_mode      = MODE_QUIET;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.priority != 6)             { FAIL("priority 6"); return; }
    if (out.pattern != RGB_PATTERN_BLUE_LOW) { FAIL("pattern BLUE_LOW"); return; }
    if (out.r != 0 || out.g != 0)      { FAIL("only blue"); return; }
    if (out.b != 51)                   { FAIL("blue=51"); return; }
    PASS();
}

// ── T09: P6 occupied + quiet_active → blue low ──
static void test_p6_occupied_quiet_active(void)
{
    TEST("T09: P6 occupied + quiet_active -> blue low");
    room_state_t st;
    fill_state(&st);
    st.wifi_connected = true;
    st.occupancy      = OCCUPANCY_OCCUPIED;
    st.user_mode      = MODE_NORMAL;
    st.quiet_active   = true;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.priority != 6)             { FAIL("priority 6"); return; }
    if (out.pattern != RGB_PATTERN_BLUE_LOW) { FAIL("pattern BLUE_LOW"); return; }
    if (out.b != 51)                   { FAIL("blue=51"); return; }
    PASS();
}

// ── T10: P7 occupied + NORMAL → green ──
static void test_p7_occupied_normal(void)
{
    TEST("T10: P7 occupied + NORMAL -> green");
    room_state_t st;
    fill_state(&st);
    st.wifi_connected = true;
    st.occupancy      = OCCUPANCY_OCCUPIED;
    st.user_mode      = MODE_NORMAL;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.priority != 7)             { FAIL("priority 7"); return; }
    if (out.pattern != RGB_PATTERN_GREEN) { FAIL("pattern GREEN"); return; }
    if (out.g != 127)                  { FAIL("green=127"); return; }
    if (out.r != 0 || out.b != 0)      { FAIL("only green"); return; }
    PASS();
}

// ── T11: P8 vacant → off ──
static void test_p8_vacant(void)
{
    TEST("T11: P8 vacant -> off");
    room_state_t st;
    fill_state(&st);
    st.wifi_connected = true;
    st.occupancy      = OCCUPANCY_VACANT;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.priority != 8)             { FAIL("priority 8"); return; }
    if (out.pattern != RGB_PATTERN_OFF) { FAIL("pattern OFF"); return; }
    if (out.r != 0 || out.g != 0 || out.b != 0) { FAIL("off"); return; }
    PASS();
}

// ── T12: UNKNOWN → dim amber (not VACANT) ──
static void test_p8_unknown(void)
{
    TEST("T12: UNKNOWN -> dim amber (not VACANT)");
    room_state_t st;
    fill_state(&st);
    st.wifi_connected = true;
    st.occupancy      = OCCUPANCY_UNKNOWN;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.priority != 8)                    { FAIL("priority 8"); return; }
    if (out.pattern != RGB_PATTERN_UNKNOWN_AMBER) { FAIL("pattern UNKNOWN_AMBER"); return; }
    if (out.r != 16 || out.g != 8 || out.b != 0) { FAIL("dim amber"); return; }
    PASS();
}

// ── T13: Priority ordering: higher priority wins ──
static void test_priority_ordering(void)
{
    TEST("T13: P1 overrides P2-P8 (sensor fail > commissioning)");
    room_state_t st;
    fill_state(&st);
    st.radar_online        = false;
    st.commissioning_active = true;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.priority != 1)           { FAIL("P1 should win over P2"); return; }
    if (out.pattern == RGB_PATTERN_BLUE_FAST_BLINK) { FAIL("should NOT be P2"); return; }
    PASS();
}

// ── T14: Priority: P2 overrides P3 ──
static void test_priority_p2_over_p3(void)
{
    TEST("T14: P2 commissioning > P3 wifi down");
    room_state_t st;
    fill_state(&st);
    st.commissioning_active = true;
    st.wifi_connected       = false;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.priority != 2)                     { FAIL("P2 should win over P3"); return; }
    if (out.pattern != RGB_PATTERN_BLUE_FAST_BLINK) { FAIL("pattern BLUE_FAST_BLINK"); return; }
    PASS();
}

// ── T15: Priority: P3 overrides P4-P8 ──
static void test_priority_p3_over_p4(void)
{
    TEST("T15: P3 wifi down > P4 env alert");
    room_state_t st;
    fill_state(&st);
    st.wifi_connected = false;
    st.env_alert      = ALERT_ACTIVE;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.priority != 3)                     { FAIL("P3 should win over P4"); return; }
    if (out.pattern != RGB_PATTERN_WHITE_SLOW_BLINK) { FAIL("pattern WHITE_SLOW_BLINK"); return; }
    PASS();
}

// ── T16: Blink timing: P1 red at exact 0, 500, 1000 ms boundaries ──
static void test_p1_blink_boundaries(void)
{
    TEST("T16: P1 blink at exact 0/500/1000 boundaries");
    room_state_t st;
    fill_state(&st);
    st.radar_online = false;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.pattern != RGB_PATTERN_RED_BLINK) { FAIL("pattern RED_BLINK"); return; }
    if (out.r == 0)                { FAIL("on at t=0"); return; }
    ui_rgb_compute(&st, 499, &out);
    if (out.r == 0)                { FAIL("on at t=499"); return; }
    ui_rgb_compute(&st, 500, &out);
    if (out.pattern != RGB_PATTERN_RED_BLINK) { FAIL("pattern RED_BLINK"); return; }
    if (out.r != 0)                { FAIL("off at t=500"); return; }
    ui_rgb_compute(&st, 999, &out);
    if (out.r != 0)                { FAIL("off at t=999"); return; }
    ui_rgb_compute(&st, 1000, &out);
    if (out.r == 0)                { FAIL("on at t=1000"); return; }
    PASS();
}

// ── T17: P2 blink at exact 0/250/500 boundaries ──
static void test_p2_blink_boundaries(void)
{
    TEST("T17: P2 blink at exact 0/250/500 boundaries");
    room_state_t st;
    fill_state(&st);
    st.commissioning_active = true;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.pattern != RGB_PATTERN_BLUE_FAST_BLINK) { FAIL("pattern BLUE_FAST_BLINK"); return; }
    if (out.b == 0)                { FAIL("on at t=0"); return; }
    ui_rgb_compute(&st, 249, &out);
    if (out.b == 0)                { FAIL("on at t=249"); return; }
    ui_rgb_compute(&st, 250, &out);
    if (out.b != 0)                { FAIL("off at t=250"); return; }
    ui_rgb_compute(&st, 499, &out);
    if (out.b != 0)                { FAIL("off at t=499"); return; }
    ui_rgb_compute(&st, 500, &out);
    if (out.b == 0)                { FAIL("on at t=500"); return; }
    PASS();
}

// ── T18: P3 blink at exact 0/1000/2000 boundaries ──
static void test_p3_blink_boundaries(void)
{
    TEST("T18: P3 blink at exact 0/1000/2000 boundaries");
    room_state_t st;
    fill_state(&st);
    st.wifi_connected = false;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.pattern != RGB_PATTERN_WHITE_SLOW_BLINK) { FAIL("pattern WHITE_SLOW_BLINK"); return; }
    if (out.r == 0)                { FAIL("on at t=0"); return; }
    ui_rgb_compute(&st, 999, &out);
    if (out.r == 0)                { FAIL("on at t=999"); return; }
    ui_rgb_compute(&st, 1000, &out);
    if (out.r != 0)                { FAIL("off at t=1000"); return; }
    ui_rgb_compute(&st, 1999, &out);
    if (out.r != 0)                { FAIL("off at t=1999"); return; }
    ui_rgb_compute(&st, 2000, &out);
    if (out.r == 0)                { FAIL("on at t=2000"); return; }
    PASS();
}

// ── T19: P1 yellow blip at exact 0/200/10000 boundaries ──
static void test_p1_yellow_blip_boundaries(void)
{
    TEST("T19: P1 yellow blip at 0/200/10000 boundaries");
    room_state_t st;
    fill_state(&st);
    st.radar_online      = true;
    st.env_sensor_online = false;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.pattern != RGB_PATTERN_RED_YELLOW_BLIP) { FAIL("pattern RED_YELLOW_BLIP"); return; }
    if (out.r != 63 || out.g != 63) { FAIL("yellow at t=0"); return; }
    ui_rgb_compute(&st, 199, &out);
    if (out.pattern != RGB_PATTERN_RED_YELLOW_BLIP) { FAIL("pattern RED_YELLOW_BLIP at 199"); return; }
    if (out.r != 63 || out.g != 63) { FAIL("yellow at t=199"); return; }
    ui_rgb_compute(&st, 200, &out);
    if (out.pattern != RGB_PATTERN_RED_BLINK)  { FAIL("pattern RED_BLINK at 200"); return; }
    if (out.r != 64 || out.g != 0)  { FAIL("red at t=200"); return; }
    ui_rgb_compute(&st, 9999, &out);
    if (out.pattern != RGB_PATTERN_RED_BLINK)  { FAIL("pattern RED_BLINK at 9999"); return; }
    if (out.g != 0)                 { FAIL("red at t=9999"); return; }
    ui_rgb_compute(&st, 10000, &out);
    if (out.pattern != RGB_PATTERN_RED_YELLOW_BLIP) { FAIL("pattern RED_YELLOW_BLIP at 10000"); return; }
    if (out.r != 63 || out.g != 63) { FAIL("yellow at t=10000"); return; }
    PASS();
}

// ── T20: Yellow blip fires on env sensor failure (regardless of radar) ──
static void test_p1_yellow_blip_both_sensors(void)
{
    TEST("T20: both offline -> yellow blip (env failure drives blip)");
    room_state_t st;
    fill_state(&st);
    st.radar_online       = false;
    st.env_sensor_online  = false;

    ui_rgb_output_t out;
    ui_rgb_compute(&st, 0, &out);
    if (out.pattern != RGB_PATTERN_RED_YELLOW_BLIP) { FAIL("pattern RED_YELLOW_BLIP"); return; }
    if (out.g != 63) { FAIL("yellow blip expected (env offline)"); return; }
    if (out.r != 63) { FAIL("yellow blip r=63 expected"); return; }
    PASS();
}

int main(void)
{
    printf("RGB priority mapping unit tests\n");
    printf("===============================\n\n");

    test_p1_sensor_fail_radar();
    test_p1_env_offline_yellow_blip();
    test_p2_commissioning();
    test_p2_not_commissioning();
    test_p3_wifi_down();
    test_p4_env_alert();
    test_p5_occupied_night();
    test_p6_occupied_quiet();
    test_p6_occupied_quiet_active();
    test_p7_occupied_normal();
    test_p8_vacant();
    test_p8_unknown();
    test_priority_ordering();
    test_priority_p2_over_p3();
    test_priority_p3_over_p4();
    test_p1_blink_boundaries();
    test_p2_blink_boundaries();
    test_p3_blink_boundaries();
    test_p1_yellow_blip_boundaries();
    test_p1_yellow_blip_both_sensors();

    printf("\n---\n");
    printf("PASS: %d  FAIL: %d  TOTAL: %d\n",
           s_pass, s_fail, s_pass + s_fail);

    return s_fail > 0 ? 1 : 0;
}
