# The QNX environment puts ARM compiler wrappers first on PATH.
ifeq ($(origin CC),default)
CC := gcc
endif
CC ?= gcc
ifeq ($(origin CXX),default)
CXX := g++
endif
CXX ?= g++
export CC CXX

CPPFLAGS += -Iinclude -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
LVGL_DIR = third_party/lvgl
LVGL_CPPFLAGS = -DLV_CONF_INCLUDE_SIMPLE -Iconfig/lvgl -isystem $(LVGL_DIR)/include
CPPFLAGS += $(LVGL_CPPFLAGS)
CFLAGS ?= -O2 -g
CXXFLAGS ?= -O2 -g
NBT_CFLAGS = -std=c99 -Wall -Wextra -Werror -Wno-deprecated-declarations -fstack-protector-all
NBT_CXXFLAGS = -std=c++11 -fno-exceptions -fno-rtti -fstack-protector-all -fstack-usage -Wall -Wextra -Werror -Wshadow -Wformat=2 -Wcast-align -Wconversion -Wsign-conversion -Wno-deprecated-declarations
LDLIBS += -lcrypto
BUILD_DIR ?= build
# Always absolute: dependency files name their targets as built, and a
# relative and an absolute spelling of one object never match (dev32's kit,
# built with an absolute BUILD_DIR, would have kept stale objects).
override BUILD_DIR := $(abspath $(BUILD_DIR))
DIST_DIR ?= dist
OBJ_DIR ?= $(BUILD_DIR)/obj
override OBJ_DIR := $(abspath $(OBJ_DIR))

MANAGER_VERSION ?=
MANAGER_SEQUENCE ?=
MANAGER_COMPAT ?=
MANAGER_SIGNING_KEY ?=
MANAGER_PUBLIC_KEY ?=
# The apps public key bundled with the manager: every application package is
# signed with its private half (tools/embed_key.py, src/package/apps_key.cpp).
APPS_PUBLIC_KEY ?=
# The desktop installer is the sibling NBTFoundry/installer checkout; only
# installer-release and windows-kit use it.
CARGO ?= cargo
NBT_INSTALLER_DIR ?= $(abspath ../installer)
INSTALLER_TARGET_DIR ?= $(abspath $(BUILD_DIR))/installer
export BUILD_DIR DIST_DIR MANAGER_VERSION MANAGER_SEQUENCE MANAGER_COMPAT
export MANAGER_SIGNING_KEY MANAGER_PUBLIC_KEY APPS_PUBLIC_KEY CARGO NBT_INSTALLER_DIR
export INSTALLER_TARGET_DIR

.PHONY: all host release installer-release test clean simulator run-simulator qnx qnx-plugin qnx-capture qnx-check plugin-resources windows-kit kit check-cxx check-static-init stack-report pre-commit format fuzz FORCE tsan
all: release

# The whole kit procedure from a clean commit: QNX checks, kit, copy to the
# exchange folder and checksums there (tools/make_kit.py; KIT_ARGS=--no-copy).
kit:
	python3 tools/make_kit.py $(KIT_ARGS)

release:
	python3 tools/build_release.py check
	$(MAKE) host qnx qnx-plugin qnx-capture
	python3 tools/build_release.py package

# The desktop installer with dist/manager.nbtr embedded (NBT_INSTALLER_DIR).
installer-release:
	python3 tools/build_release.py installer

windows-kit:
	python3 tools/build_windows_kit.py

check-cxx:
	python3 tools/check_cxx.py

check-static-init: host
	python3 tools/check_static_init.py

stack-report: host
	python3 tools/stack_report.py

format:
	python3 tools/pre_commit.py --format

pre-commit:
	python3 tools/pre_commit.py --all

NBT_SRC = $(sort $(wildcard src/*.cpp src/*/*.cpp))
NBT_LIB_C = src/capture/control.c src/capture/classifier.c src/capture/diag.c
HOST_OBJ = $(patsubst %.cpp,$(OBJ_DIR)/host/%.o,$(NBT_SRC)) $(patsubst %.c,$(OBJ_DIR)/host/%.o,$(NBT_LIB_C))
TESTING_OBJ = $(patsubst %.cpp,$(OBJ_DIR)/testing/%.o,$(NBT_SRC)) $(patsubst %.c,$(OBJ_DIR)/testing/%.o,$(NBT_LIB_C))
HOST_LIB = $(BUILD_DIR)/libnbt.a
TESTING_LIB = $(BUILD_DIR)/libnbt-testing.a

$(BUILD_DIR):
	mkdir -p $@

# Rewritten only when the bundled key changes.
$(BUILD_DIR)/apps-key.inc: FORCE | $(BUILD_DIR)
	python3 tools/embed_key.py '$(APPS_PUBLIC_KEY)' $@
APPS_KEY_OBJ = $(foreach kind,host testing qnx,$(OBJ_DIR)/$(kind)/src/package/apps_key.o)
$(APPS_KEY_OBJ): $(BUILD_DIR)/apps-key.inc
$(APPS_KEY_OBJ): CPPFLAGS += -I$(BUILD_DIR)
$(APPS_KEY_OBJ): QNX_CXXFLAGS += -I$(BUILD_DIR)

$(OBJ_DIR)/host/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(NBT_CXXFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/testing/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(NBT_CXXFLAGS) -DNBT_TESTING -MMD -MP -c $< -o $@

$(OBJ_DIR)/host/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(NBT_CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR)/testing/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(NBT_CFLAGS) -DNBT_TESTING -MMD -MP -c $< -o $@

$(HOST_LIB): $(HOST_OBJ) | $(BUILD_DIR)
	rm -f $@
	$(AR) rcs $@ $^

$(TESTING_LIB): $(TESTING_OBJ) | $(BUILD_DIR)
	rm -f $@
	$(AR) rcs $@ $^

LAUNCHER_TESTING_OBJ = $(filter-out $(OBJ_DIR)/testing/src/gui/ui_renderer.o $(OBJ_DIR)/testing/src/gui/gui_session.o,$(TESTING_OBJ))
LAUNCHER_TESTING_LIB = $(BUILD_DIR)/libnbt-launcher-testing.a

$(LAUNCHER_TESTING_LIB): $(LAUNCHER_TESTING_OBJ) | $(BUILD_DIR)
	rm -f $@
	$(AR) rcs $@ $^

LVGL_SRC = $(sort $(shell find $(LVGL_DIR)/src -name '*.c' 2>/dev/null))
LVGL_CFLAGS = $(CFLAGS) -fstack-protector-all -w
LVGL_DEPS = config/lvgl/lv_conf.h $(LVGL_DIR)/include/lvgl/lvgl.h

$(LVGL_DIR)/include/lvgl/lvgl.h:
	@echo "LVGL is missing: run 'git submodule update --init'" >&2; exit 1

$(BUILD_DIR)/lvgl/%.o: $(LVGL_DIR)/src/%.c $(LVGL_DEPS)
	@mkdir -p $(dir $@)
	$(CC) $(LVGL_CPPFLAGS) -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $(LVGL_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/liblvgl.a: $(patsubst $(LVGL_DIR)/src/%.c,$(BUILD_DIR)/lvgl/%.o,$(LVGL_SRC))
	rm -f $@
	$(AR) rcs $@ $^

HOST_BINS = phone-test most-monitor core-test bridge-test most-test target-test nbt-verify nbt-verify-release release-copy-probe manager-state-test manager-store-probe manager-stage-probe manager-setup-probe manager-plugin-probe manager-plugin-swap-probe manager-refresh-probe fs-probe transaction-probe frame-test ui-test hold-test controller-config-test log-test test-app-host session-test http-test process-probe manager-probe managerd-test client-probe ui-probe qr-probe most-input-probe manager-gui-host launcher-host
HOST_BINS += app-log-test controller-feed-test gamepad-test usb-gamepad-test focus-test app-pause-test input-trace-test capture-test capture-shim-test capture-install-probe hmi-restart-test
host: $(addprefix $(BUILD_DIR)/,$(HOST_BINS)) $(BUILD_DIR)/libfake_dsi.so $(BUILD_DIR)/libfake_dsi_base.so $(BUILD_DIR)/libnbt-capture-observer.so $(BUILD_DIR)/libnbt-capture-active.so

define HOST_LINK
$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)
endef

$(BUILD_DIR)/phone-test: $(OBJ_DIR)/host/tests/test_phone.o $(HOST_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/bridge-test: $(OBJ_DIR)/host/tests/test_bridge.o $(HOST_LIB)
	$(HOST_LINK)

$(OBJ_DIR)/usb-shim/%.o: src/gamepad/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Itests/fake_usb -D__QNX__ $(CXXFLAGS) $(NBT_CXXFLAGS) -MMD -MP -c $< -o $@
$(OBJ_DIR)/host/tests/test_usb_gamepad.o: tests/test_usb_gamepad.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) -Itests/fake_usb $(CXXFLAGS) $(NBT_CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD_DIR)/usb-gamepad-test: $(OBJ_DIR)/host/tests/test_usb_gamepad.o $(OBJ_DIR)/usb-shim/usb_device.o $(OBJ_DIR)/usb-shim/usb_transport.o $(OBJ_DIR)/usb-shim/usb_discovery.o $(OBJ_DIR)/usb-shim/usb_configuration.o $(OBJ_DIR)/usb-shim/usbdi.o $(HOST_LIB)
	$(HOST_LINK) -pthread -ldl

$(BUILD_DIR)/gamepad-test: $(OBJ_DIR)/testing/tests/test_gamepad.o $(TESTING_LIB) $(BUILD_DIR)/liblvgl.a
	$(HOST_LINK) -pthread -ldl -lm
$(BUILD_DIR)/most-test: $(OBJ_DIR)/host/tests/test_most.o $(HOST_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/target-test: $(OBJ_DIR)/host/tests/test_target.o $(HOST_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/nbt-verify: $(OBJ_DIR)/host/tools/verify.o $(HOST_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/nbt-verify-release: $(OBJ_DIR)/host/tools/verify_release.o $(HOST_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/release-copy-probe: $(OBJ_DIR)/host/tests/release_copy_probe.o $(HOST_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/manager-state-test: $(OBJ_DIR)/host/tests/test_manager_state.o $(HOST_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/frame-test: $(OBJ_DIR)/host/tests/test_frame.o $(HOST_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/hold-test: $(OBJ_DIR)/host/tests/test_hmi_hold.o $(HOST_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/focus-test: $(OBJ_DIR)/host/tests/test_focus_sender.o $(HOST_LIB)
	$(HOST_LINK) -pthread
$(BUILD_DIR)/app-pause-test: $(OBJ_DIR)/host/tests/test_app_pause.o $(HOST_LIB)
	$(HOST_LINK) -pthread -ldl
$(BUILD_DIR)/input-trace-test: $(OBJ_DIR)/host/tests/test_input_trace.o $(HOST_LIB)
	$(HOST_LINK) -pthread
$(BUILD_DIR)/controller-feed-test: $(OBJ_DIR)/host/tests/test_controller_feed.o $(HOST_LIB)
	$(HOST_LINK) -pthread
$(BUILD_DIR)/controller-config-test: $(OBJ_DIR)/host/tests/test_controller_config.o $(HOST_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/session-test: $(OBJ_DIR)/host/tests/test_session.o $(HOST_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/http-test: $(OBJ_DIR)/host/tests/test_http.o $(HOST_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/core-test: $(OBJ_DIR)/testing/tests/core_test.o $(TESTING_LIB)
	$(HOST_LINK) -pthread
$(BUILD_DIR)/qr-probe: $(OBJ_DIR)/host/tests/qr_probe.o $(HOST_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/most-monitor: $(OBJ_DIR)/host/apps/most-monitor/main.o $(HOST_LIB)
	$(HOST_LINK)

$(BUILD_DIR)/most-input-probe: $(OBJ_DIR)/host/tests/most_input_probe.o $(HOST_LIB)
	$(HOST_LINK) -pthread -ldl
$(BUILD_DIR)/nbt-simulator: $(OBJ_DIR)/host/tools/simulator.o $(HOST_LIB) $(BUILD_DIR)/liblvgl.a
	$(HOST_LINK) -lX11 -lm

$(BUILD_DIR)/ui-test: $(OBJ_DIR)/host/tests/test_ui.o $(HOST_LIB) $(BUILD_DIR)/liblvgl.a
	$(HOST_LINK) -lm

$(BUILD_DIR)/manager-store-probe: $(OBJ_DIR)/testing/tests/manager_store_probe.o $(TESTING_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/manager-stage-probe: $(OBJ_DIR)/testing/tests/manager_stage_probe.o $(TESTING_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/manager-setup-probe: $(OBJ_DIR)/testing/tests/manager_setup_probe.o $(TESTING_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/manager-plugin-probe: $(OBJ_DIR)/testing/tests/manager_plugin_probe.o $(TESTING_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/manager-plugin-swap-probe: $(OBJ_DIR)/testing/tests/manager_plugin_swap_probe.o $(TESTING_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/manager-refresh-probe: $(OBJ_DIR)/testing/tests/manager_refresh_probe.o $(TESTING_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/fs-probe: $(OBJ_DIR)/testing/tests/fs_probe.o $(TESTING_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/transaction-probe: $(OBJ_DIR)/testing/tests/transaction_probe.o $(TESTING_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/log-test: $(OBJ_DIR)/testing/tests/test_log.o $(TESTING_LIB)
	$(HOST_LINK) -pthread
$(BUILD_DIR)/app-log-test: $(OBJ_DIR)/testing/tests/test_app_log.o $(TESTING_LIB)
	$(HOST_LINK) -Wl,--wrap=fstat,--wrap=write,--wrap=fsync,--wrap=lseek
$(BUILD_DIR)/process-probe: $(OBJ_DIR)/testing/tests/process_probe.o $(TESTING_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/manager-probe: $(OBJ_DIR)/testing/tests/manager_probe.o $(TESTING_LIB)
	$(HOST_LINK) -pthread -ldl
$(BUILD_DIR)/managerd-test: $(OBJ_DIR)/testing/apps/managerd/main.o $(TESTING_LIB)
	$(HOST_LINK) -pthread -ldl
$(BUILD_DIR)/client-probe: $(OBJ_DIR)/testing/tests/client_probe.o $(TESTING_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/ui-probe: $(OBJ_DIR)/testing/tests/ui_probe.o $(TESTING_LIB) $(BUILD_DIR)/liblvgl.a
	$(HOST_LINK) -lm
$(BUILD_DIR)/manager-gui-host: $(OBJ_DIR)/testing/apps/manager/main.o $(OBJ_DIR)/testing/tests/fake_screen.o $(TESTING_LIB) $(BUILD_DIR)/liblvgl.a
	$(HOST_LINK) -pthread -ldl -lm
$(BUILD_DIR)/launcher-host: $(OBJ_DIR)/testing/apps/manager/launcher.o $(LAUNCHER_TESTING_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/test-app-host: $(OBJ_DIR)/testing/apps/test/main.o $(OBJ_DIR)/testing/tests/fake_screen.o $(TESTING_LIB)
	$(HOST_LINK) -pthread -ldl

$(BUILD_DIR)/libfake_dsi.so: tests/fake_dsi.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(NBT_CFLAGS) -fPIC $< $(LDFLAGS) -shared -o $@
$(BUILD_DIR)/libfake_dsi_base.so: tests/fake_dsi_base.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(NBT_CFLAGS) -fPIC $< $(LDFLAGS) -shared -pthread -o $@
$(BUILD_DIR)/libnbt-capture-observer.so: src/capture/control.c src/capture/classifier.c src/capture/shim_env.c src/capture/shim_diag.c src/capture/shim.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(NBT_CFLAGS) -DNBT_TESTING -DNBT_CAPTURE_OBSERVER=1 -fPIC -fvisibility=hidden $^ $(LDFLAGS) -shared -Wl,-z,defs -pthread -ldl -o $@
$(BUILD_DIR)/libnbt-capture-active.so: src/capture/control.c src/capture/classifier.c src/capture/shim_env.c src/capture/shim_diag.c src/capture/shim.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(NBT_CFLAGS) -DNBT_TESTING -DNBT_CAPTURE_OBSERVER=0 -fPIC -fvisibility=hidden $^ $(LDFLAGS) -shared -Wl,-z,defs -pthread -ldl -o $@
$(BUILD_DIR)/capture-test: $(OBJ_DIR)/testing/tests/test_capture.o $(TESTING_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/hmi-restart-test: $(OBJ_DIR)/testing/tests/test_hmi_restart.o $(TESTING_LIB)
	$(HOST_LINK)
$(BUILD_DIR)/capture-shim-test: $(OBJ_DIR)/testing/tests/test_capture_shim.o $(BUILD_DIR)/libfake_dsi_base.so $(TESTING_LIB)
	$(CC) $(LDFLAGS) -Wl,-rpath,$(abspath $(BUILD_DIR)) -o $@ $^ -pthread -ldl
$(BUILD_DIR)/capture-install-probe: $(OBJ_DIR)/testing/tests/capture_install_probe.o $(TESTING_LIB)
	$(HOST_LINK)

simulator: $(BUILD_DIR)/nbt-simulator
run-simulator: $(BUILD_DIR)/nbt-simulator
	$(BUILD_DIR)/nbt-simulator $(SCALE)

test: host check-cxx
	$(BUILD_DIR)/phone-test
	$(BUILD_DIR)/core-test
	$(BUILD_DIR)/bridge-test
	$(BUILD_DIR)/most-test
	$(BUILD_DIR)/gamepad-test
	$(BUILD_DIR)/controller-feed-test
	$(BUILD_DIR)/usb-gamepad-test
	$(BUILD_DIR)/hmi-restart-test
	$(BUILD_DIR)/target-test
	$(BUILD_DIR)/manager-state-test
	$(BUILD_DIR)/frame-test
	$(BUILD_DIR)/ui-test
	$(BUILD_DIR)/hold-test
	$(BUILD_DIR)/focus-test
	$(BUILD_DIR)/app-pause-test
	$(BUILD_DIR)/input-trace-test
	$(BUILD_DIR)/controller-config-test
	$(BUILD_DIR)/log-test
	$(BUILD_DIR)/app-log-test
	$(BUILD_DIR)/session-test
	$(BUILD_DIR)/http-test
	$(BUILD_DIR)/capture-test
	NBT_BUILD_DIR=$(abspath $(BUILD_DIR)) python3 -m unittest discover -s tests -v

$(BUILD_DIR)/fuzz-test: $(OBJ_DIR)/testing/tests/fuzz.o $(TESTING_LIB)
	$(HOST_LINK)

# The threads under ThreadSanitizer: the GUI's worker channel
# (src/gui/worker_channel.cpp; a request answered after 3.5 s while the UI loop
# keeps polling, five times), the worker's Wi-Fi Direct preparation
# (src/worker/wifi_direct_job.cpp; the Wi-Fi Direct session tests) and its job
# for verification and copies (src/worker/worker_job.cpp; the transaction
# tests). Any report fails.
TSAN_BUILD_DIR ?= $(BUILD_DIR)/tsan
tsan:
	$(MAKE) BUILD_DIR=$(TSAN_BUILD_DIR) \
		CFLAGS='-O1 -g -fsanitize=thread -fno-omit-frame-pointer' \
		CXXFLAGS='-O1 -g -fsanitize=thread -fno-omit-frame-pointer' \
		LDFLAGS='-fsanitize=thread' $(TSAN_BUILD_DIR)/client-probe $(TSAN_BUILD_DIR)/managerd-test \
		$(TSAN_BUILD_DIR)/transaction-probe $(TSAN_BUILD_DIR)/manager-probe \
		$(TSAN_BUILD_DIR)/libfake_dsi.so
	@set -e; rm -f $(TSAN_BUILD_DIR)/tsan-report.*; \
	export TSAN_OPTIONS="halt_on_error=1 second_deadlock_stack=1 log_path=$(TSAN_BUILD_DIR)/tsan-report"; \
	for run in 1 2 3 4 5; do $(TSAN_BUILD_DIR)/client-probe keepalive; done; \
	cd tests && NBT_BUILD_DIR=$(TSAN_BUILD_DIR) python3 -m unittest -q test_wifi_direct test_transaction; \
	if ls $(TSAN_BUILD_DIR)/tsan-report.* >/dev/null 2>&1; then cat $(TSAN_BUILD_DIR)/tsan-report.*; exit 1; fi; \
	echo "tsan: OK"

FUZZ_BUILD_DIR ?= $(BUILD_DIR)/fuzz
FUZZ_SECONDS ?= 1
FUZZ_TARGETS = gamepad-wire gamepad-gip gamepad-sony http phone-signature phone-pair phone-context control package-header package-manifest release-manifest settings-schema settings-values controller-config installed-record journal most-codec most-joiner ccs most-policy bridge-requirement bridge-status wfd-most capture-control capture-stream starter-config
fuzz:
	$(MAKE) BUILD_DIR=$(FUZZ_BUILD_DIR) \
		CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
		CXXFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
		LDFLAGS='-fsanitize=address,undefined -no-pie' $(FUZZ_BUILD_DIR)/fuzz-test
	@set -e; for target in $(FUZZ_TARGETS); do \
		for input in tests/corpus/$$target/*; do \
			if test -f "$$input"; then \
				ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
					$(FUZZ_BUILD_DIR)/fuzz-test --replay $$target "$$input"; \
			fi; \
		done; \
		ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
			NBT_FUZZ_CORPUS_DIR=$(abspath tests/corpus) $(FUZZ_BUILD_DIR)/fuzz-test $$target $(FUZZ_SECONDS); \
	done

QCC ?= qcc
QNX_SCREEN_LIBDIR ?=
RHMI_LIBDIR ?=
export QCC QNX_SCREEN_LIBDIR RHMI_LIBDIR
QNX_VARIANT = -Vgcc_ntoarmv7le_gpp
# qcc passes gcc options it does not know only through -Wc. It pipes the
# compiler's output, so -auxbase-strip puts each .su file beside its object
# (stack-report) instead of in the current directory.
QNX_LVGL_CPPFLAGS = -DLV_CONF_INCLUDE_SIMPLE -Iconfig/lvgl -Wc,-isystem,$(LVGL_DIR)/include
QNX_CXXFLAGS = $(QNX_VARIANT) -Wc,-std=c++11 -Wc,-fno-exceptions -Wc,-fno-rtti -fstack-protector-all -fstack-usage -Wc,-auxbase-strip,$@ -O2 -D_QNX_SOURCE -Wc,-Wall -Wc,-Wextra -Wc,-Werror -Iinclude $(QNX_LVGL_CPPFLAGS)
QNX_AR ?= ntoarmv7-ar
QNX_READELF ?= ntoarmv7-readelf
QNX_NM ?= ntoarmv7-nm
QNX_SIZE ?= ntoarmv7-size
QNX_STRIP ?= ntoarmv7-strip
# QNX binaries are linked here with their symbols, to read crash addresses;
# the stripped copies in BUILD_DIR are the ones packaged and shipped.
QNX_UNSTRIPPED = $(BUILD_DIR)/qnx-unstripped
QNX_EXECUTABLES = nbt-managerd-qnx test-v1-qnx test-v2-qnx test-v3-broken-qnx nbt-manager-launcher nbt-manager-gui nbt-verify-release-qnx most-monitor-qnx
# qcc ignores -MMD/-MP (it once wrote a file named "-MP"), so no QNX object
# was rebuilt when a header changed: dev31 shipped a GUI whose objects
# disagreed on UiController's layout. The preprocessor takes them directly.
QNX_DEPFLAGS = -Wp,-MMD,$(@:.o=.d) -Wp,-MP -Wp,-MT,$@
QNX_OBJ = $(patsubst %.cpp,$(OBJ_DIR)/qnx/%.o,$(NBT_SRC)) $(patsubst %.c,$(OBJ_DIR)/qnx/%.o,$(NBT_LIB_C))
QNX_LIB = $(BUILD_DIR)/libnbt-qnx.a

$(OBJ_DIR)/qnx/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(QCC) $(QNX_CXXFLAGS) $(QNX_DEPFLAGS) -c $< -o $@

$(OBJ_DIR)/qnx/%.o: %.c
	@mkdir -p $(dir $@)
	$(QCC) -O2 -Vgcc_ntoarmv7le -D_QNX_SOURCE -Wc,-std=c99 -Wc,-Wall -Wc,-Wextra -Wc,-Werror -fstack-protector-all -Iinclude $(QNX_DEPFLAGS) -c $< -o $@

$(QNX_LIB): $(QNX_OBJ) | $(BUILD_DIR)
	rm -f $@
	$(QNX_AR) rcs $@ $^

$(BUILD_DIR)/lvgl-qnx/%.o: $(LVGL_DIR)/src/%.c $(LVGL_DEPS)
	@mkdir -p $(dir $@)
	$(QCC) -O2 -Vgcc_ntoarmv7le -D_QNX_SOURCE -Wc,-std=c99 -fstack-protector-all -w $(QNX_LVGL_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/liblvgl-qnx.a: $(patsubst $(LVGL_DIR)/src/%.c,$(BUILD_DIR)/lvgl-qnx/%.o,$(LVGL_SRC))
	rm -f $@
	$(QNX_AR) rcs $@ $^

$(OBJ_DIR)/qnx/apps/test/main-v1.o: apps/test/main.cpp
	@mkdir -p $(dir $@)
	$(QCC) $(QNX_CXXFLAGS) '-DNBT_TEST_VERSION="v1"' $(QNX_DEPFLAGS) -c $< -o $@
$(OBJ_DIR)/qnx/apps/test/main-v2.o: apps/test/main.cpp
	@mkdir -p $(dir $@)
	$(QCC) $(QNX_CXXFLAGS) '-DNBT_TEST_VERSION="v2"' $(QNX_DEPFLAGS) -c $< -o $@
$(OBJ_DIR)/qnx/apps/test/main-v3.o: apps/test/main.cpp
	@mkdir -p $(dir $@)
	$(QCC) $(QNX_CXXFLAGS) '-DNBT_TEST_VERSION="v3"' -DNBT_TEST_FAIL_AFTER=10 $(QNX_DEPFLAGS) -c $< -o $@
# The GUI's banner shows MANAGER_VERSION; the stamp changes only with its value.
$(BUILD_DIR)/manager-version: FORCE | $(BUILD_DIR)
	@echo '$(MANAGER_VERSION)' | cmp -s - $@ || echo '$(MANAGER_VERSION)' > $@
FORCE:
$(OBJ_DIR)/qnx/apps/manager/main-versioned.o: apps/manager/main.cpp $(BUILD_DIR)/manager-version
	@mkdir -p $(dir $@)
	$(QCC) $(QNX_CXXFLAGS) $(if $(MANAGER_VERSION),'-DNBT_MANAGER_VERSION="$(MANAGER_VERSION)"') $(QNX_DEPFLAGS) -c $< -o $@

qnx: $(QNX_LIB) $(QNX_TARGET)/armle-v7/lib/libusbdi.a $(BUILD_DIR)/liblvgl-qnx.a $(OBJ_DIR)/qnx/apps/managerd/main.o $(OBJ_DIR)/qnx/apps/manager/launcher.o $(OBJ_DIR)/qnx/apps/test/main-v1.o $(OBJ_DIR)/qnx/apps/test/main-v2.o $(OBJ_DIR)/qnx/apps/test/main-v3.o $(OBJ_DIR)/qnx/apps/manager/main-versioned.o $(OBJ_DIR)/qnx/tools/verify_release.o $(OBJ_DIR)/qnx/apps/most-monitor/main.o | $(BUILD_DIR)
	test -n "$(QNX_SCREEN_LIBDIR)"
	mkdir -p $(QNX_UNSTRIPPED)
	$(QCC) $(QNX_VARIANT) -lang-c++ $(OBJ_DIR)/qnx/apps/managerd/main.o $(QNX_LIB) $(QNX_TARGET)/armle-v7/lib/libusbdi.a -lcrypto -lsocket -o $(QNX_UNSTRIPPED)/nbt-managerd-qnx
	$(QCC) $(QNX_VARIANT) -lang-c++ $(OBJ_DIR)/qnx/apps/test/main-v1.o $(QNX_LIB) -L$(QNX_SCREEN_LIBDIR) -lscreen -lcrypto -o $(QNX_UNSTRIPPED)/test-v1-qnx
	$(QCC) $(QNX_VARIANT) -lang-c++ $(OBJ_DIR)/qnx/apps/test/main-v2.o $(QNX_LIB) -L$(QNX_SCREEN_LIBDIR) -lscreen -lcrypto -o $(QNX_UNSTRIPPED)/test-v2-qnx
	$(QCC) $(QNX_VARIANT) -lang-c++ $(OBJ_DIR)/qnx/apps/test/main-v3.o $(QNX_LIB) -L$(QNX_SCREEN_LIBDIR) -lscreen -lcrypto -o $(QNX_UNSTRIPPED)/test-v3-broken-qnx
	$(QCC) $(QNX_VARIANT) -lang-c++ $(OBJ_DIR)/qnx/apps/manager/launcher.o $(QNX_LIB) -lcrypto -o $(QNX_UNSTRIPPED)/nbt-manager-launcher
	$(QCC) $(QNX_VARIANT) -lang-c++ $(OBJ_DIR)/qnx/apps/manager/main-versioned.o $(QNX_LIB) $(QNX_TARGET)/armle-v7/lib/libusbdi.a $(BUILD_DIR)/liblvgl-qnx.a -L$(QNX_SCREEN_LIBDIR) -lscreen -lcrypto -lsocket -lm -o $(QNX_UNSTRIPPED)/nbt-manager-gui
	$(QCC) $(QNX_VARIANT) -lang-c++ $(OBJ_DIR)/qnx/tools/verify_release.o $(QNX_LIB) -lcrypto -o $(QNX_UNSTRIPPED)/nbt-verify-release-qnx
	$(QCC) $(QNX_VARIANT) -lang-c++ $(OBJ_DIR)/qnx/apps/most-monitor/main.o $(QNX_LIB) -lsocket -lcrypto -o $(QNX_UNSTRIPPED)/most-monitor-qnx
	for name in $(QNX_EXECUTABLES); do $(QNX_STRIP) -o $(BUILD_DIR)/$$name $(QNX_UNSTRIPPED)/$$name || exit 1; done
	cp $(OBJ_DIR)/qnx/src/gui/worker_client.o $(BUILD_DIR)/client-qnx.o
	cp $(OBJ_DIR)/qnx/src/launcher/slot_state.o $(BUILD_DIR)/manager-state-qnx.o
	cp $(OBJ_DIR)/qnx/src/launcher/slot_store.o $(BUILD_DIR)/manager-store-qnx.o
	cp $(OBJ_DIR)/qnx/src/launcher/stager.o $(BUILD_DIR)/manager-stage-qnx.o
	cp $(OBJ_DIR)/qnx/src/launcher/manager_setup.o $(BUILD_DIR)/manager-setup-qnx.o
	cp $(OBJ_DIR)/qnx/src/launcher/plugin_installer.o $(BUILD_DIR)/manager-plugin-qnx.o

qnx-capture: | $(BUILD_DIR)
	mkdir -p $(QNX_UNSTRIPPED)
	$(QCC) -O2 -Vgcc_ntoarmv7le -D_QNX_SOURCE -DNBT_CAPTURE_OBSERVER=0 -Wc,-std=c99 -Wc,-Wall -Wc,-Wextra -Wc,-Werror -fPIC -fvisibility=hidden -shared -Iinclude -Wl,-soname,libnbt-capture.so -Wl,-z,defs -Wl,--exclude-libs,ALL -o $(QNX_UNSTRIPPED)/libnbt-capture.so src/capture/control.c src/capture/classifier.c src/capture/shim_env.c src/capture/shim_diag.c src/capture/shim.c
	$(QNX_STRIP) -o $(BUILD_DIR)/libnbt-capture.so $(QNX_UNSTRIPPED)/libnbt-capture.so

qnx-check: qnx qnx-plugin qnx-capture
	QNX_READELF="$(QNX_READELF)" QNX_NM="$(QNX_NM)" QNX_SIZE="$(QNX_SIZE)" python3 tools/check_qnx_artifacts.py

plugin-resources: | $(BUILD_DIR)
	mkdir -p $(BUILD_DIR)/NbtManager
	python3 plugin/make_resources.py $(BUILD_DIR)/NbtManager
	cp plugin/NbtManager.manifest plugin/NbtManager.xml $(BUILD_DIR)/NbtManager/

qnx-plugin: plugin-resources
	test -f "$(RHMI_LIBDIR)/libAppFramework.A.so"
	test -f "$(RHMI_LIBDIR)/libCoreFramework.A.so"
	mkdir -p $(QNX_UNSTRIPPED)
	$(QCC) $(QNX_VARIANT) -D_QNX_SOURCE -O2 -Wc,-std=c++11 -Wc,-fno-exceptions -Wc,-fno-rtti -Wc,-Wall -Wc,-Wextra -Wc,-Werror -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -Iinclude -c src/gui/hmi_hold.cpp -o $(BUILD_DIR)/hmi-hold-plugin.o
	$(QCC) $(QNX_VARIANT) -D_QNX_SOURCE -O2 -Wc,-std=c++11 -Wc,-fno-exceptions -Wc,-fno-rtti -Wc,-Wall -Wc,-Wextra -Wc,-Werror -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -Iinclude -c src/gui/focus_sender.cpp -o $(BUILD_DIR)/focus-sender-plugin.o
	$(QCC) $(QNX_VARIANT) -D_QNX_SOURCE -O2 -Wc,-std=c++11 -Wc,-fno-exceptions -Wc,-fno-rtti -Wc,-Wall -Wc,-Wextra -Wc,-Werror -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -Iinclude -c src/log/log.cpp -o $(BUILD_DIR)/log-plugin.o
	$(QCC) $(QNX_VARIANT) -D_QNX_SOURCE -O2 -Wc,-std=c++11 -Wc,-fno-exceptions -Wc,-fno-rtti -Wc,-Wall -Wc,-Wextra -Wc,-Werror -fPIC -fvisibility=hidden -Iinclude -c src/gui/phone_wake.cpp -o $(BUILD_DIR)/phone-wake-plugin.o
	$(QCC) -O2 -Vgcc_ntoarmv7le -D_QNX_SOURCE -Wc,-std=c99 -Wc,-Wall -Wc,-Wextra -Wc,-Werror -fPIC -fvisibility=hidden -Iinclude -c src/capture/control.c -o $(BUILD_DIR)/capture-control-plugin.o
	$(QCC) -O2 -Vgcc_ntoarmv7le -D_QNX_SOURCE -Wc,-std=c99 -Wc,-Wall -Wc,-Wextra -Wc,-Werror -fPIC -fvisibility=hidden -Iinclude -c src/capture/diag.c -o $(BUILD_DIR)/capture-diag-plugin.o
	$(QCC) $(QNX_VARIANT) -D_QNX_SOURCE -O2 -Wall -Wextra -Werror -fPIC -fvisibility=hidden -fvisibility-inlines-hidden -fno-exceptions -fno-rtti -shared -Iinclude -Wl,-soname,libNbtManager.so -o $(QNX_UNSTRIPPED)/libNbtManager.so plugin/plugin.cpp plugin/launcher_spawn.cpp plugin/phone_runtime.cpp plugin/phone_display.cpp plugin/focus_events.cpp plugin/hmi_threads.cpp $(BUILD_DIR)/phone-wake-plugin.o $(BUILD_DIR)/hmi-hold-plugin.o $(BUILD_DIR)/focus-sender-plugin.o $(BUILD_DIR)/log-plugin.o $(BUILD_DIR)/capture-control-plugin.o $(BUILD_DIR)/capture-diag-plugin.o $(RHMI_LIBDIR)/libAppFramework.A.so $(RHMI_LIBDIR)/libCoreFramework.A.so -Wl,--allow-shlib-undefined -Wl,-z,defs -Wl,--exclude-libs,ALL -lang-c++
	$(QNX_STRIP) -o $(BUILD_DIR)/NbtManager/libNbtManager.so $(QNX_UNSTRIPPED)/libNbtManager.so

DEPS = $(shell find $(OBJ_DIR) -name '*.d' 2>/dev/null)
-include $(DEPS)

clean:
	rm -rf $(BUILD_DIR) obj
