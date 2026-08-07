// Host tests for the pure, target-independent health diagnostic classifiers.

#include <stdio.h>

#include "health_diag.h"

static int failures;

#define TEST(name) \
    do { printf("  %-52s", name); } while (0)

#define EXPECT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("FAIL (%s)\n", message); \
            failures++; \
            return; \
        } \
    } while (0)

#define PASS() \
    do { printf("PASS\n"); } while (0)

static void test_reset_classification(void)
{
    TEST("reset codes map to stable diagnostic classes");
    EXPECT(health_diag_classify_reset_reason(HEALTH_DIAG_RST_POWERON) ==
               HEALTH_DIAG_RESET_NORMAL,
           "power-on");
    EXPECT(health_diag_classify_reset_reason(HEALTH_DIAG_RST_SW) ==
               HEALTH_DIAG_RESET_SOFTWARE,
           "software");
    EXPECT(health_diag_classify_reset_reason(HEALTH_DIAG_RST_PANIC) ==
               HEALTH_DIAG_RESET_PANIC,
           "panic");
    EXPECT(health_diag_classify_reset_reason(HEALTH_DIAG_RST_INT_WDT) ==
               HEALTH_DIAG_RESET_WATCHDOG,
           "interrupt watchdog");
    EXPECT(health_diag_classify_reset_reason(HEALTH_DIAG_RST_TASK_WDT) ==
               HEALTH_DIAG_RESET_WATCHDOG,
           "task watchdog");
    EXPECT(health_diag_classify_reset_reason(HEALTH_DIAG_RST_WDT) ==
               HEALTH_DIAG_RESET_WATCHDOG,
           "other watchdog");
    EXPECT(health_diag_classify_reset_reason(HEALTH_DIAG_RST_BROWNOUT) ==
               HEALTH_DIAG_RESET_BROWNOUT,
           "brownout");
    EXPECT(health_diag_classify_reset_reason(0x7f) ==
               HEALTH_DIAG_RESET_UNKNOWN,
           "unknown");
    PASS();
}

static void test_class_names(void)
{
    TEST("reset and heap class names are stable and non-empty");
    EXPECT(health_diag_reset_class_name(HEALTH_DIAG_RESET_WATCHDOG)[0] != '\0',
           "reset class name");
    EXPECT(health_diag_heap_class_name(HEALTH_DIAG_HEAP_LOW)[0] != '\0',
           "heap class name");
    PASS();
}

static void test_heap_threshold(void)
{
    TEST("minimum free heap threshold is log-only and exact at boundary");
    EXPECT(health_diag_classify_heap(0) == HEALTH_DIAG_HEAP_LOW, "zero");
    EXPECT(health_diag_classify_heap(HEALTH_DIAG_HEAP_WARN_BYTES - 1U) ==
               HEALTH_DIAG_HEAP_LOW,
           "below threshold");
    EXPECT(health_diag_classify_heap(HEALTH_DIAG_HEAP_WARN_BYTES) ==
               HEALTH_DIAG_HEAP_OK,
           "at threshold");
    EXPECT(health_diag_classify_heap(HEALTH_DIAG_HEAP_WARN_BYTES + 1U) ==
               HEALTH_DIAG_HEAP_OK,
           "above threshold");
    PASS();
}

int main(void)
{
    printf("health diagnostic classifier unit tests\n");
    printf("=========================================\n\n");

    test_reset_classification();
    test_class_names();
    test_heap_threshold();

    printf("\n---\nPASS: %d  FAIL: %d\n", failures == 0 ? 3 : 0, failures);
    return failures == 0 ? 0 : 1;
}
