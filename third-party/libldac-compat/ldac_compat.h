// third-party/libldac-compat/ldac_compat.h
// MSVC compatibility shims for libldac (Sony LDAC encoder, Apache 2.0)
#pragma once
#ifdef _MSC_VER
#  include <BaseTsd.h>
   typedef SSIZE_T ssize_t;   // in case any libldac source uses ssize_t

// GCC attributes are not supported on MSVC; suppress them globally for the ldac sources.
// bitalloc_sub_ldac.c uses __attribute__((unused)) — drop it at preprocessor level.
#  define __attribute__(x)     // MSVC: drop all __attribute__ annotations
#endif
