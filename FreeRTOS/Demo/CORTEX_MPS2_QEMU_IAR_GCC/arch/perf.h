/*
 * ============================================================================
 * File: arch/perf.h
 * Description: Performance Profiling Hooks for LwIP Stack
 * Target Platform: ARM Cortex-M3 (QEMU MPS2 AN385 / STM32)
 *
 * Provides empty no-op macros for LwIP internal performance profiling hooks.
 * Can be populated with Cortex-M DWT cycle counter reads if benchmarking is required.
 * ============================================================================
 */

#ifndef __ARCH_PERF_H__
#define __ARCH_PERF_H__

/* No-op performance measurement macros */
#define PERF_START      /* Measurement start hook */
#define PERF_STOP(x)    /* Measurement stop and log hook */

#endif /* __ARCH_PERF_H__ */
