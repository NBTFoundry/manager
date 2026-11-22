# SPDX-License-Identifier: MPL-2.0
# Build stamps: objects rebuild when the command that compiles them changes
# (another compiler, toolchain or flags), not only when their sources do, and
# only then. Each kind of object (host, sanitizer, QNX, module...) has a stamp
# file holding its compiler, that compiler's version and its flags; the kind's
# objects list the stamp as a prerequisite. The stamp is rewritten while make
# reads the makefiles, only when its text differs or a toolchain file named
# with it is newer, so a second make finds nothing to do (make -q passes).
# Sub-makes reading it at once (make -j) agree: it is replaced whole.
# GNU make 3.81 or later. A copy of the NBT App SDK's nbt-stamp.mk.
#
#   HOST_STAMP := $(OBJ_DIR)/host.stamp
#   $(OBJ_DIR)/host/%.o: %.c $(HOST_STAMP)
#   	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
#   ...
#   # at the end of the Makefile, once every variable the text uses is set:
#   $(call nbt_stamp,$(HOST_STAMP),$(CC) $(CFLAGS),$(CC))
#
# Headers are -MMD's (-MP) business, and a toolchain's own headers (-isystem)
# change with the toolchain: name its VERSION file as the fourth argument.
#
# $(call nbt_stamp,stamp file,text,compilers whose --version counts,
#   files whose being newer counts) expands to nothing.
#
# A target whose recipe fails is deleted, so no half-written object or
# archive (a full disk, an interrupted tool) looks up to date next time.
.DELETE_ON_ERROR:
nbt_stamp = $(shell f='$(1)'; t='$(subst ','\'',$(strip $(2)))'; \
	$(foreach c,$(3),t="$$t; $$($(c) --version 2>/dev/null | head -n 1)";) \
	if test "$$(cat "$$f" 2>/dev/null)" != "$$t"$(foreach x,$(4), || test '$(x)' -nt "$$f"); then \
	test -f "$$f" && echo "build stamp changed, rebuilding its objects: $$f" >&2; \
	mkdir -p "$${f%/*}" && printf '%s\n' "$$t" > "$$f.$$$$" && mv -f "$$f.$$$$" "$$f"; fi)
