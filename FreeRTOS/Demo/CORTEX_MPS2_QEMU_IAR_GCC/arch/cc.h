/*
 * ============================================================================
 * File: arch/cc.h
 * Description: Compiler & Architecture Type Definitions for LwIP on ARM Cortex-M
 * Target Platform: ARM Cortex-M3 (GNU ARM Embedded Toolchain / GCC)
 *
 * Configures platform-specific data types, byte ordering (Little Endian),
 * structure packing attributes, and diagnostic output macros for the LwIP stack.
 * ============================================================================
 */

#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

/* Standard toolchain headers */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <errno.h>

/*
 * ============================================================================
 * Primitive Integer Data Type Mappings (Fixed Width)
 * ============================================================================
 */
typedef uint8_t   u8_t;
typedef int8_t    s8_t;
typedef uint16_t  u16_t;
typedef int16_t   s16_t;
typedef uint32_t  u32_t;
typedef int32_t   s32_t;
typedef uintptr_t mem_ptr_t;

/*
 * ============================================================================
 * Printf Format Specifiers for Fixed-Width Types
 * ============================================================================
 */
#define U16_F "hu"
#define S16_F "hd"
#define X16_F "hx"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "u"

/*
 * ============================================================================
 * Endianness Configuration
 * ARM Cortex-M architecture executes in Little-Endian byte order.
 * ============================================================================
 */
#ifndef BYTE_ORDER
  #define BYTE_ORDER LITTLE_ENDIAN
#endif

/*
 * ============================================================================
 * GCC Compiler Structure Packing Directives
 * Ensures protocol headers (Ethernet, IP, TCP) are packed without byte padding.
 * ============================================================================
 */
#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_STRUCT   __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

/*
 * ============================================================================
 * Platform Diagnostics & Assertion Macros
 * ============================================================================
 */
#define LWIP_PLATFORM_DIAG(x)   do { printf x; } while(0)
#define LWIP_PLATFORM_ASSERT(x) do { printf("[LWIP ASSERT] \"%s\" failed at line %d in %s\n", \
                                            x, __LINE__, __FILE__); fflush(NULL); abort(); } while(0)

#endif /* LWIP_ARCH_CC_H */
