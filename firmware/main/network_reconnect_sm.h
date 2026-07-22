// PrivacySense Matter Room Hub - network_reconnect_sm.h
//
// Pure-logic Wi-Fi reconnect state machine.
//
// State model:
//   DISCONNECTED ──STA_START(provisioned)─▶ CONNECTING ──GOT_IP─▶ CONNECTED
//        ▲                                       │                  │
//        │                                       │ DISCONNECTED     │ DISCONNECTED
//        │                                       ▼                  │
//        │                                    DISCONNECTED ◀─────────┘
//        │                                       │
//        │                                       │ auth_fail ×3
//        │                                       ▼
//        │                                    STOPPED ──PROVISIONED─▶ CONNECTING
//        └─────────────────────────────────────────────────────────────┘
//
//   RECONFIGURING: waiting for self-induced DISCONNECTED before WIFI_CONNECT.
//   - DISCONNECTED → CONNECTING + WIFI_CONNECT
//   - GOT_IP → CONNECTED (ESP-Matter owns connect; local SM tracks state)
//   - Timeout: caller checks elapsed time, not SM-internal timer.
//
// Note (Phase 3 Step 2): ESP-Matter's ESPWiFiDriver is the sole Wi-Fi owner.
// GOT_IP always transitions to CONNECTED regardless of previous state, so
// the local SM stays in sync with ESP-Matter-initiated connect/reconnect.
//
// Action failure: NET_SM_EVENT_ACTION_FAILED with fail_type.
// START_TIMER failure clears timer_armed AND sets timer_arm_pending so the
// caller can reschedule.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NET_SM_RECONFIGURING_TIMEOUT_MS  5000U

typedef enum {
    NET_SM_STATE_DISCONNECTED = 0,
    NET_SM_STATE_CONNECTING,
    NET_SM_STATE_CONNECTED,
    NET_SM_STATE_STOPPED,
    NET_SM_STATE_RECONFIGURING,
} net_sm_state_t;

typedef enum {
    NET_SM_EVENT_STA_START = 0,
    NET_SM_EVENT_DISCONNECTED,
    NET_SM_EVENT_GOT_IP,
    NET_SM_EVENT_TIMER_FIRED,
    NET_SM_EVENT_PROVISIONED,
    NET_SM_EVENT_ACTION_FAILED,
    NET_SM_EVENT_RECONFIG_TIMEOUT,
} net_sm_event_type_t;

typedef enum {
    NET_SM_FAIL_WIFI_CONNECT = 0,
    NET_SM_FAIL_WIFI_DISCONNECT,
    NET_SM_FAIL_START_TIMER,
} net_sm_action_fail_t;

typedef struct {
    net_sm_event_type_t type;
    uint8_t  disconnect_reason;
    bool     has_saved_creds;
    net_sm_action_fail_t fail_type;
    uint8_t  timer_generation;   // valid for TIMER_FIRED — stale if != sm->timer_generation
} net_sm_event_t;

typedef enum {
    NET_SM_STATUS_DISCONNECTED = 0,
    NET_SM_STATUS_CONNECTED,
    NET_SM_STATUS_PROVISIONED,
    NET_SM_STATUS_STOPPED,
} net_sm_status_t;

#define NET_SM_ACT_STOP_TIMER       (1u << 0)
#define NET_SM_ACT_START_TIMER      (1u << 1)
#define NET_SM_ACT_WIFI_CONNECT     (1u << 2)
#define NET_SM_ACT_WIFI_DISCONNECT  (1u << 3)
#define NET_SM_ACT_NOTIFY_STATUS    (1u << 4)

typedef struct {
    uint32_t         flags;
    uint32_t         timer_delay_ms;
    net_sm_status_t  notify_status;
} net_sm_action_t;

typedef struct {
    net_sm_state_t state;
    uint8_t        reconnect_attempts;
    uint8_t        auth_fail_attempts;
    bool           provisioned;
    bool           timer_armed;
    bool           timer_arm_pending;   // set when START_TIMER fails; caller must reschedule
    uint8_t        timer_generation;    // incremented on each successful START_TIMER
    uint32_t       reconfig_start_ms;
} net_sm_t;

void net_sm_init(net_sm_t *sm, bool has_saved_creds);
bool net_sm_step(net_sm_t *sm, const net_sm_event_t *ev, net_sm_action_t *out_action);
uint32_t net_sm_compute_backoff_ms(uint8_t attempt);
bool net_sm_reason_is_auth_fail(uint8_t reason);

#ifdef __cplusplus
}
#endif
