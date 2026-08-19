/*
 * ============================================================================
 * File: arch/sys_arch.h
 * Description: LwIP System Architecture Primitive Type Bindings for FreeRTOS
 * Target Platform: ARM Cortex-M3 (QEMU MPS2 AN385 / STM32)
 *
 * This header maps LwIP's abstract synchronization, messaging, and thread
 * primitive types directly to concrete FreeRTOS kernel object handles.
 * ============================================================================
 */

#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

/* FreeRTOS Kernel Core Headers */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "queue.h"

/* Null handle constants representing uninitialized LwIP primitives */
#define SYS_MBOX_NULL   NULL
#define SYS_SEM_NULL    NULL

/*
 * LwIP Abstract Type to FreeRTOS Handle Mappings:
 * - sys_sem_t: Counting semaphore handle
 * - sys_mutex_t: Priority-inheritance mutex handle
 * - sys_mbox_t: FreeRTOS message queue handle for inter-task pointer passing
 * - sys_thread_t: FreeRTOS task handle (TCB address)
 * - sys_prot_t: Protection level type for critical sections
 */
typedef SemaphoreHandle_t sys_sem_t;
typedef SemaphoreHandle_t sys_mutex_t;
typedef QueueHandle_t     sys_mbox_t;
typedef TaskHandle_t      sys_thread_t;
typedef u32_t             sys_prot_t;

#endif /* LWIP_ARCH_SYS_ARCH_H */
