# SPDX-License-Identifier: MPL-2.0
# The kits' test application (apps/test/main.cpp) on the NBT App SDK at
# NBT_SDK_DIR, built without the QNX SDP in its two forms, each as v1, v2 and
# a v3 that fails 5 s after it starts:
#   $(BUILD_DIR)/test-app/test-<v>          an ordinary QNX 6.5 program
#   $(BUILD_DIR)/test-app/test-runner-<v>.so a module for the release's nbt-runner
# Run from the manager's Makefile (make test-apps); tools/build_windows_kit.py
# packages them. The NBT toolchain: "make toolchain" in the SDK, or NBT_TOOLCHAIN.
SHELL := /bin/bash
BUILD_DIR ?= build
override BUILD_DIR := $(abspath $(BUILD_DIR))
OUT := $(BUILD_DIR)/test-app
OBJ_DIR := $(OUT)/obj
PYTHON ?= python3
NBT_SDK_DIR ?= ../sdk
# The SDK build qnx-runner uses too.
NBT_SDK_BUILD_DIR := $(BUILD_DIR)/sdk
include $(NBT_SDK_DIR)/nbt-sdk.mk
ifeq ($(value nbt_stamp),)
$(error $(NBT_SDK_ROOT) has no nbt-stamp.mk: update that SDK checkout)
endif
stamp = $(OBJ_DIR)/$(1).stamp

THIS := apps/test/test-app.mk
VERSIONS := v1 v2 v3
# v3 is the broken one: it ends before the health check's 10 healthy seconds.
version_defs = '-DNBT_TEST_VERSION="$(1)"' $(if $(filter v3,$(1)),-DNBT_TEST_FAIL_AFTER=5)
APP_CXXFLAGS := -O2 -std=c++11 -Wall -Wextra -Werror -fno-exceptions -fno-rtti
QNX_COMPILE = $(NBT_QNX_CXX) $(NBT_QNX_CXXFLAGS) $(NBT_QNX_SDK_INC) $(APP_CXXFLAGS)
MOD_COMPILE = $(NBT_MOD_CXX) $(NBT_MOD_CXXFLAGS) $(APP_CXXFLAGS)

NATIVE := $(foreach v,$(VERSIONS),$(OUT)/test-$(v))
RUNNER := $(foreach v,$(VERSIONS),$(OUT)/test-runner-$(v).so)
QNX_OBJS := $(foreach v,$(VERSIONS),$(OBJ_DIR)/qnx/main-$(v).o)
MOD_OBJS := $(foreach v,$(VERSIONS),$(OBJ_DIR)/mod/main-$(v).o)

.PHONY: all native runner check
all: native runner
native: $(NATIVE)
runner: $(RUNNER)

# --- An ordinary QNX 6.5 program (the toolchain's QNX target) ---
$(QNX_OBJS): $(OBJ_DIR)/qnx/main-%.o: apps/test/main.cpp $(THIS) $(call stamp,qnx) | nbt-sdk-qnx
	@mkdir -p $(dir $@)
	$(QNX_COMPILE) $(call version_defs,$*) '-DNBT_TEST_FORM="native"' -MMD -MP -c $< -o $@

$(addsuffix -unstripped,$(NATIVE)): $(OUT)/test-%-unstripped: $(OBJ_DIR)/qnx/main-%.o $(NBT_SDK_FRONT_QNX_LIBS)
	$(call nbt_qnx_link,$@,$(filter %.o %.a,$^),$(NBT_SDK_QNX_LDLIBS))

$(NATIVE): $(OUT)/test-%: $(OUT)/test-%-unstripped
	$(NBT_LLVM_BIN)/llvm-strip -o $@ $<

# --- A module, run by nbt-runner (bridge 1.7, capability runner) ---
$(MOD_OBJS): $(OBJ_DIR)/mod/main-%.o: apps/test/main.cpp $(THIS) $(call stamp,mod) | nbt-sdk-mod
	@mkdir -p $(dir $@)
	$(MOD_COMPILE) $(call version_defs,$*) '-DNBT_TEST_FORM="runner"' -MMD -MP -c $< -o $@

$(RUNNER): $(OUT)/test-runner-%.so: $(OBJ_DIR)/mod/main-%.o $(NBT_SDK_MOD_DEPS)
	$(call nbt_module_link,$@,$<,MPL-2.0)

# The car runs QNX 6.5.0 SP1: the program's imports must exist there; libKD
# calls its kdMain. (Each module was checked when it was linked.)
QNX65_SP1_SYSROOT ?= $(HOME)/src/tools/qnx-6.5sp1-tools/sdk/target/qnx6
check: all
	@for f in $(NATIVE); do $(NBT_LLVM_BIN)/llvm-nm -D --defined-only $$f-unstripped | grep -qw kdMain \
		|| { echo "$$f: kdMain is not exported"; exit 1; }; done
	@if test -d "$(QNX65_SP1_SYSROOT)"; then \
		$(PYTHON) $(NBT_SDK_ROOT)/tools/check_qnx_imports.py --sysroot "$(QNX65_SP1_SYSROOT)" \
			--readelf $(NBT_MOD_READELF) $(addsuffix -unstripped,$(NATIVE)); \
	else echo "skipped: no QNX 6.5.0 SP1 sysroot at $(QNX65_SP1_SYSROOT)"; fi

-include $(wildcard $(OBJ_DIR)/*/*.d)

# The stamps, now that every flag is set (the SDK's nbt-stamp.mk).
$(call nbt_stamp,$(call stamp,qnx),$(QNX_COMPILE),,$(NBT_TOOLCHAIN)/VERSION)
$(call nbt_stamp,$(call stamp,mod),$(MOD_COMPILE),,$(NBT_TOOLCHAIN)/VERSION)
