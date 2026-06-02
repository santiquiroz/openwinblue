// third-party/libsbc-compat/sbc_compat.h
// MSVC compatibility shims for libsbc (BlueZ sbc, LGPL 2.1)
#pragma once
#ifdef _MSC_VER
#  include <stdint.h>          // int32_t etc. used by sbc_math.h before <sys/types.h> resolves them
#  include <BaseTsd.h>
   typedef SSIZE_T ssize_t;   // sbc.h uses ssize_t for return values

// GCC attributes are not supported on MSVC; suppress them globally for the sbc sources.
// sbc_private.h redefines SBC_EXPORT after our /FI header, so we suppress __attribute__
// at the preprocessor level instead of defining SBC_EXPORT directly.
#  define __attribute__(x)     // MSVC: drop all __attribute__ annotations

// Windows x86/x64 is always little-endian.
// sbc.c uses __BYTE_ORDER / __LITTLE_ENDIAN guards (Linux glibc convention).
#  ifndef __LITTLE_ENDIAN
#    define __LITTLE_ENDIAN 1234
#  endif
#  ifndef __BIG_ENDIAN
#    define __BIG_ENDIAN 4321
#  endif
#  ifndef __BYTE_ORDER
#    define __BYTE_ORDER __LITTLE_ENDIAN
#  endif
#endif
