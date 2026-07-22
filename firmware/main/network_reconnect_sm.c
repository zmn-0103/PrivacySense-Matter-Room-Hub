// PrivacySense Matter Room Hub - network_reconnect_sm.c
//
// Pure-logic implementation. See network_reconnect_sm.h for the state model.

#include "network_reconnect_sm.h"

#define WIFI_REASON_AUTH_FAIL                202u
#define WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT    15u
#define WIFI_REASON_HANDSHAKE_TIMEOUT        204u
#define WIFI_REASON_MIC_FAILURE               14u
#define WIFI_REASON_802_1X_AUTH_FAILED        23u

#define NET_SM_AUTH_FAIL_MAX_ATTEMPTS  3u
#define NET_SM_BACKOFF_BASE_MS   1000U
#define NET_SM_BACKOFF_MAX_MS   60000U

void net_sm_init(net_sm_t *sm, bool has_saved_creds)
{
    sm->state              = NET_SM_STATE_DISCONNECTED;
    sm->reconnect_attempts = 0;
    sm->auth_fail_attempts = 0;
    sm->provisioned        = has_saved_creds;
    sm->timer_armed        = false;
    sm->timer_arm_pending  = false;
    sm->timer_generation   = 0;
    sm->reconfig_start_ms  = 0;
}

bool net_sm_reason_is_auth_fail(uint8_t reason)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_802_1X_AUTH_FAILED:
        return true;
    default:
        return false;
    }
}

uint32_t net_sm_compute_backoff_ms(uint8_t attempt)
{
    if (attempt == 0) return 0;
    if (attempt > 6) return NET_SM_BACKOFF_MAX_MS;
    return NET_SM_BACKOFF_BASE_MS * ((uint32_t)1u << (attempt - 1u));
}

static void stop_pending_timer(net_sm_t *sm, uint32_t *flags)
{
    if (sm->timer_armed) {
        *flags |= NET_SM_ACT_STOP_TIMER;
        sm->timer_armed = false;
    }
}

static bool schedule_backoff(net_sm_t *sm, net_sm_action_t *out)
{
    if (sm->reconnect_attempts < 255u) sm->reconnect_attempts++;
    out->flags         |= NET_SM_ACT_START_TIMER;
    out->timer_delay_ms = net_sm_compute_backoff_ms(sm->reconnect_attempts);
    sm->timer_armed     = true;
    sm->timer_arm_pending = false;
    sm->timer_generation++;   // increment generation for new timer
    return true;
}

bool net_sm_step(net_sm_t *sm, const net_sm_event_t *ev,
                 net_sm_action_t *out)
{
    out->flags          = 0;
    out->timer_delay_ms = 0;
    out->notify_status  = NET_SM_STATUS_DISCONNECTED;

    switch (ev->type) {

    case NET_SM_EVENT_STA_START: {
        if (!ev->has_saved_creds) return false;
        sm->provisioned = true;
        sm->state       = NET_SM_STATE_CONNECTING;
        out->flags      = NET_SM_ACT_WIFI_CONNECT;
        return true;
    }

    case NET_SM_EVENT_GOT_IP: {
        // GOT_IP always means the link is up — transition to CONNECTED
        // regardless of the previous state. This is critical when ESP-Matter
        // is the sole Wi-Fi owner: it connects / reconnects independently
        // (via ESPWiFiDriver or ESP-IDF auto-connect) while the local SM may
        // be in DISCONNECTED, STOPPED, or RECONFIGURING state. The previous
        // state gate caused s_connected_atomic and room_state.wifi_connected
        // to never update after ESP-Matter-initiated reconnects (Reviewer AI,
        // Phase 3 Step 2).
        sm->state               = NET_SM_STATE_CONNECTED;
        sm->reconnect_attempts  = 0;
        sm->auth_fail_attempts  = 0;
        sm->timer_arm_pending   = false;
        stop_pending_timer(sm, &out->flags);
        out->flags         |= NET_SM_ACT_NOTIFY_STATUS;
        out->notify_status  = NET_SM_STATUS_CONNECTED;
        return true;
    }

    case NET_SM_EVENT_TIMER_FIRED: {
        // Issue 2 fix: only accept if generation matches current armed timer.
        // Stale callbacks from previous timer cycles are silently dropped.
        if (ev->timer_generation != sm->timer_generation) {
            return false;   // stale timer — ignore
        }
        sm->timer_armed       = false;
        sm->timer_arm_pending = false;
        if (sm->state != NET_SM_STATE_DISCONNECTED) return false;
        if (!sm->provisioned) return false;
        sm->state  = NET_SM_STATE_CONNECTING;
        out->flags = NET_SM_ACT_WIFI_CONNECT;
        return true;
    }

    case NET_SM_EVENT_RECONFIG_TIMEOUT: {
        if (sm->state != NET_SM_STATE_RECONFIGURING) return false;
        sm->state = NET_SM_STATE_CONNECTING;
        out->flags |= NET_SM_ACT_WIFI_CONNECT;
        return true;
    }

    case NET_SM_EVENT_PROVISIONED: {
        sm->provisioned        = true;
        sm->reconnect_attempts = 0;
        sm->auth_fail_attempts = 0;
        stop_pending_timer(sm, &out->flags);
        sm->timer_arm_pending = false;

        if (sm->state == NET_SM_STATE_DISCONNECTED ||
            sm->state == NET_SM_STATE_STOPPED) {
            sm->state = NET_SM_STATE_CONNECTING;
            out->flags |= NET_SM_ACT_WIFI_CONNECT;
            out->flags |= NET_SM_ACT_NOTIFY_STATUS;
            out->notify_status = NET_SM_STATUS_PROVISIONED;
            return true;
        }

        sm->state = NET_SM_STATE_RECONFIGURING;
        sm->reconfig_start_ms = 0;
        out->flags |= NET_SM_ACT_WIFI_DISCONNECT;
        out->flags |= NET_SM_ACT_NOTIFY_STATUS;
        out->notify_status = NET_SM_STATUS_PROVISIONED;
        return true;
    }

    case NET_SM_EVENT_DISCONNECTED: {
        if (sm->state == NET_SM_STATE_STOPPED) return false;

        if (sm->state == NET_SM_STATE_RECONFIGURING) {
            sm->state = NET_SM_STATE_CONNECTING;
            out->flags |= NET_SM_ACT_WIFI_CONNECT;
            return true;
        }

        sm->state = NET_SM_STATE_DISCONNECTED;
        stop_pending_timer(sm, &out->flags);
        out->flags         |= NET_SM_ACT_NOTIFY_STATUS;
        out->notify_status  = NET_SM_STATUS_DISCONNECTED;

        if (net_sm_reason_is_auth_fail(ev->disconnect_reason)) {
            sm->auth_fail_attempts++;
            if (sm->auth_fail_attempts >= NET_SM_AUTH_FAIL_MAX_ATTEMPTS) {
                sm->state = NET_SM_STATE_STOPPED;
                out->notify_status = NET_SM_STATUS_STOPPED;
                return true;
            }
        }
        schedule_backoff(sm, out);
        return true;
    }

    case NET_SM_EVENT_ACTION_FAILED: {
        switch (ev->fail_type) {
        case NET_SM_FAIL_WIFI_CONNECT:
            if (sm->state == NET_SM_STATE_CONNECTING) {
                sm->state = NET_SM_STATE_DISCONNECTED;
                schedule_backoff(sm, out);
                return true;
            }
            break;
        case NET_SM_FAIL_WIFI_DISCONNECT:
            if (sm->state == NET_SM_STATE_RECONFIGURING) {
                sm->state = NET_SM_STATE_CONNECTING;
                out->flags |= NET_SM_ACT_WIFI_CONNECT;
                return true;
            }
            break;
        case NET_SM_FAIL_START_TIMER:
            sm->timer_armed       = false;
            sm->timer_arm_pending = true;
            break;
        }
        return false;
    }

    default:
        return false;
    }
}
