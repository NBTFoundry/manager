// SPDX-License-Identifier: MPL-2.0
#ifndef NBT_CORE_CONFIG_HPP
#define NBT_CORE_CONFIG_HPP

#if defined(__GNUC__)
#define NBT_WARN_UNUSED __attribute__((warn_unused_result))
#define NBT_PRINTF(format, arguments) __attribute__((format(printf, format, arguments)))
#else
#define NBT_WARN_UNUSED
#define NBT_PRINTF(format, arguments)
#endif

#endif
