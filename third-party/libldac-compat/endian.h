// third-party/libldac-compat/endian.h
// Stub for MSVC: provides endianness macros that libldac needs from <endian.h>
#pragma once
#ifndef __LITTLE_ENDIAN
#  define __LITTLE_ENDIAN 1234
#endif
#ifndef __BIG_ENDIAN
#  define __BIG_ENDIAN 4321
#endif
#ifndef __BYTE_ORDER
// Windows x86/x64 is always little-endian
#  define __BYTE_ORDER __LITTLE_ENDIAN
#endif
