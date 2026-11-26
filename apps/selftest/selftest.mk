# SPDX-License-Identifier: MPL-2.0
# NBT Manager's self test applications (apps/selftest, docs/self-test.md) on
# the NBT App SDK at NBT_SDK_DIR, built without the QNX SDP in their three
# forms, and for the desktop:
#   $(BUILD_DIR)/selftest/selftest-native        an ordinary QNX 6.5 program
#   $(BUILD_DIR)/selftest/selftest-guest.guest   nbt-runner's child process
#   $(BUILD_DIR)/selftest/selftest-module.so     a module for nbt-runner
#   $(BUILD_DIR)/selftest/selftest-host          the module's code on Linux
# Run from the manager's Makefile (make selftest-apps); tools/build_release.py
# packages and signs them (<form>.args) into the release.
SHELL := /bin/bash
BUILD_DIR ?= build
override BUILD_DIR := $(abspath $(BUILD_DIR))
OUT := $(BUILD_DIR)/selftest
OBJ_DIR := $(OUT)/obj
PYTHON ?= python3
NBT_SDK_DIR ?= ../sdk
NBT_SDK_BUILD_DIR := $(BUILD_DIR)/sdk
include $(NBT_SDK_DIR)/nbt-sdk.mk
stamp = $(OBJ_DIR)/$(1).stamp

DIR := apps/selftest
THIS := $(DIR)/selftest.mk
SOURCES := main report probes
HEADERS := $(DIR)/report.hpp $(DIR)/probes.hpp
# The value after option $(2) in args file $(1), "" when it has none.
arg = $(shell awk 'f { print; exit } $$0 == "$(2)" { f = 1 }' $(1))
# A form's declared capabilities, which its bridge check expects granted.
caps = $(call arg,$(DIR)/$(1).args,--bridge-uses),$(or $(call arg,$(DIR)/$(1).args,--bridge-optional),$(call arg,$(DIR)/common.args,--bridge-optional))
form_defs = '-DNBT_SELFTEST_FORM="$(1)"' '-DNBT_SELFTEST_CAPS="$(call caps,$(1))"' \
	-DNBT_SELFTEST_INTERACTIVE=$(if $(filter module,$(1)),1,0)
APP_CXXFLAGS := -O2 -std=c++11 -Wall -Wextra -Werror -fno-exceptions -fno-rtti
QNX_COMPILE = $(NBT_QNX_CXX) $(NBT_QNX_CXXFLAGS) $(NBT_QNX_SDK_INC) $(APP_CXXFLAGS)
MOD_COMPILE = $(NBT_MOD_CXX) $(NBT_MOD_CXXFLAGS) $(APP_CXXFLAGS)
HOST_CXX ?= g++
HOST_COMPILE = $(HOST_CXX) $(NBT_SDK_INC) $(APP_CXXFLAGS) -g

NATIVE := $(OUT)/selftest-native
GUEST := $(OUT)/selftest-guest.guest
MODULE := $(OUT)/selftest-module.so
HOST := $(OUT)/selftest-host
objs = $(foreach s,$(SOURCES),$(OBJ_DIR)/$(1)/$(s).o)

.PHONY: all qnx host check
all: qnx host
qnx: $(NATIVE) $(GUEST) $(MODULE)
host: $(HOST)

$(call objs,native): $(OBJ_DIR)/native/%.o: $(DIR)/%.cpp $(HEADERS) $(THIS) $(DIR)/native.args $(call stamp,qnx) | nbt-sdk-qnx
	@mkdir -p $(dir $@)
	$(QNX_COMPILE) $(call form_defs,native) -c $< -o $@

$(NATIVE)-unstripped: $(call objs,native) $(NBT_SDK_FRONT_QNX_LIBS)
	$(call nbt_qnx_link,$@,$(filter %.o %.a,$^),$(NBT_SDK_QNX_LDLIBS))

$(NATIVE): $(NATIVE)-unstripped
	$(NBT_LLVM_BIN)/llvm-strip -o $@ $<

$(call objs,guest) $(call objs,module): $(OBJ_DIR)/%.o: $(HEADERS) $(THIS) $(call stamp,mod) | nbt-sdk-mod
$(call objs,guest): $(OBJ_DIR)/guest/%.o: $(DIR)/%.cpp $(DIR)/guest.args
	@mkdir -p $(dir $@)
	$(MOD_COMPILE) $(call form_defs,guest) -c $< -o $@
$(call objs,module): $(OBJ_DIR)/module/%.o: $(DIR)/%.cpp $(DIR)/module.args
	@mkdir -p $(dir $@)
	$(MOD_COMPILE) $(call form_defs,module) -c $< -o $@

$(GUEST): $(call objs,guest) $(NBT_SDK_MOD_DEPS)
	$(call nbt_guest_link,$@,$(call objs,guest),MPL-2.0)

$(MODULE): $(call objs,module) $(NBT_SDK_MOD_DEPS)
	$(call nbt_module_link,$@,$(call objs,module),MPL-2.0)

$(call objs,host): $(OBJ_DIR)/host/%.o: $(DIR)/%.cpp $(HEADERS) $(THIS) $(DIR)/module.args
	@mkdir -p $(dir $@)
	$(HOST_COMPILE) $(call form_defs,module) -c $< -o $@

$(HOST): $(call objs,host) $(NBT_SDK_FRONT_HOST_LIBS)
	$(HOST_CXX) $(filter %.o,$^) $(NBT_SDK_FRONT_HOST_LIBS) $(NBT_SDK_HOST_LDLIBS) -o $@

# The car runs QNX 6.5.0 SP1: the program's imports must exist there; libKD
# calls its kdMain. (The module and the guest were checked when linked.)
QNX65_SP1_SYSROOT ?= $(HOME)/src/tools/qnx-6.5sp1-tools/sdk/target/qnx6
check: qnx
	@$(NBT_LLVM_BIN)/llvm-nm -D --defined-only $(NATIVE)-unstripped | grep -qw kdMain \
		|| { echo "$(NATIVE): kdMain is not exported"; exit 1; }
	@if test -d "$(QNX65_SP1_SYSROOT)"; then \
		$(PYTHON) $(NBT_SDK_ROOT)/tools/check_qnx_imports.py --sysroot "$(QNX65_SP1_SYSROOT)" \
			--readelf $(NBT_MOD_READELF) $(NATIVE)-unstripped; \
	else echo "skipped: no QNX 6.5.0 SP1 sysroot at $(QNX65_SP1_SYSROOT)"; fi

# The stamps, now that every flag is set (the SDK's nbt-stamp.mk).
$(call nbt_stamp,$(call stamp,qnx),$(QNX_COMPILE),,$(NBT_TOOLCHAIN)/VERSION)
$(call nbt_stamp,$(call stamp,mod),$(MOD_COMPILE),,$(NBT_TOOLCHAIN)/VERSION)
