CC       = gcc
CFLAGS   = -Wall -Wextra -Werror -I. -I../../firmware/main
BUILD    = build

# Source file paths
UI_RGB_SRC          = ../../firmware/main/ui_rgb.c
MODE_TRANS_SRC      = ../../firmware/main/mode_transition.c
OCC_SM_SRC          = ../../firmware/main/occ_sm.c
ENV_ALERT_SM_SRC    = ../../firmware/main/env_alert_sm.c
NIGHT_WINDOW_SM_SRC = ../../firmware/main/night_window_sm.c
NET_RECONNECT_SM_SRC = ../../firmware/main/network_reconnect_sm.c

# Object files
UI_RGB_OBJ          = $(BUILD)/ui_rgb.o
MODE_TRANS_OBJ      = $(BUILD)/mode_transition.o
OCC_SM_OBJ          = $(BUILD)/occ_sm.o
ENV_ALERT_SM_OBJ    = $(BUILD)/env_alert_sm.o
NIGHT_WINDOW_SM_OBJ = $(BUILD)/night_window_sm.o
NET_RECONNECT_SM_OBJ = $(BUILD)/network_reconnect_sm.o
TEST_OCC_OBJ        = $(BUILD)/test_occ_sm.o
TEST_UI_OBJ         = $(BUILD)/test_ui_rgb.o
TEST_BTN_OBJ        = $(BUILD)/test_button_mode.o
TEST_ENV_OBJ        = $(BUILD)/test_env_alert_sm.o
TEST_NIGHT_OBJ      = $(BUILD)/test_night_window_sm.o
TEST_NET_OBJ        = $(BUILD)/test_network_reconnect_sm.o

# Test executables
TEST_OCC   = $(BUILD)/test_occ_sm
TEST_UI    = $(BUILD)/test_ui_rgb
TEST_BTN   = $(BUILD)/test_button_mode
TEST_ENV   = $(BUILD)/test_env_alert_sm
TEST_NIGHT = $(BUILD)/test_night_window_sm
TEST_NET   = $(BUILD)/test_network_reconnect_sm

.PHONY: all clean

all: $(BUILD) $(TEST_OCC) $(TEST_UI) $(TEST_BTN) $(TEST_ENV) $(TEST_NIGHT) $(TEST_NET)
	@echo "=== Running occ_sm tests ===" && $(TEST_OCC)
	@echo ""
	@echo "=== Running ui_rgb tests ===" && $(TEST_UI)
	@echo ""
	@echo "=== Running button_mode tests ===" && $(TEST_BTN)
	@echo ""
	@echo "=== Running env_alert_sm tests ===" && $(TEST_ENV)
	@echo ""
	@echo "=== Running night_window_sm tests ===" && $(TEST_NIGHT)
	@echo ""
	@echo "=== Running network_reconnect_sm tests ===" && $(TEST_NET)

$(BUILD):
	mkdir -p $(BUILD)

# ── occ_sm tests ──
$(TEST_OCC): $(TEST_OCC_OBJ) $(OCC_SM_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_OCC_OBJ): test_occ_sm.c ../../firmware/main/occ_sm.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OCC_SM_OBJ): $(OCC_SM_SRC) ../../firmware/main/occ_sm.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# ── ui_rgb tests ──
$(TEST_UI): $(TEST_UI_OBJ) $(UI_RGB_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_UI_OBJ): test_ui_rgb.c ../../firmware/main/ui_rgb.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(UI_RGB_OBJ): $(UI_RGB_SRC) ../../firmware/main/ui_rgb.h \
               ../../firmware/main/room_state.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# ── button mode tests ──
$(TEST_BTN): $(TEST_BTN_OBJ) $(MODE_TRANS_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_BTN_OBJ): test_button_mode.c ../../firmware/main/mode_transition.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(MODE_TRANS_OBJ): $(MODE_TRANS_SRC) \
                   ../../firmware/main/mode_transition.h \
                   ../../firmware/main/room_state.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# ── env_alert_sm tests (pure logic, no FreeRTOS shim needed) ──
$(TEST_ENV): $(TEST_ENV_OBJ) $(ENV_ALERT_SM_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_ENV_OBJ): test_env_alert_sm.c ../../firmware/main/env_alert_sm.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(ENV_ALERT_SM_OBJ): $(ENV_ALERT_SM_SRC) ../../firmware/main/env_alert_sm.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# ── night_window_sm tests (pure logic, no FreeRTOS shim needed) ──
$(TEST_NIGHT): $(TEST_NIGHT_OBJ) $(NIGHT_WINDOW_SM_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_NIGHT_OBJ): test_night_window_sm.c ../../firmware/main/night_window_sm.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(NIGHT_WINDOW_SM_OBJ): $(NIGHT_WINDOW_SM_SRC) ../../firmware/main/night_window_sm.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# ── network_reconnect_sm tests (pure logic, no FreeRTOS shim needed) ──
$(TEST_NET): $(TEST_NET_OBJ) $(NET_RECONNECT_SM_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(TEST_NET_OBJ): test_network_reconnect_sm.c ../../firmware/main/network_reconnect_sm.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(NET_RECONNECT_SM_OBJ): $(NET_RECONNECT_SM_SRC) ../../firmware/main/network_reconnect_sm.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD)
