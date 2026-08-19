/*
 * ============================================================================
 * File: sys_arch.c
 * Description: LwIP System Architecture OS Adaptation Layer for FreeRTOS
 * Target Platform: ARM Cortex-M (QEMU MPS2 AN385 / Cortex-M3 / STM32)
 *
 * This file provides the mandatory operating system abstraction functions
 * required by the LwIP TCP/IP stack in multithreaded OS mode (NO_SYS = 0).
 * It bridges LwIP stack concepts directly to FreeRTOS kernel primitives:
 *   - Counting Semaphores : sys_sem_*  -> xSemaphoreCreateCounting()
 *   - Mutual Exclusion    : sys_mutex_* -> xSemaphoreCreateMutex()
 *   - Message Mailboxes   : sys_mbox_*  -> xQueueCreate() (FreeRTOS Queues)
 *   - OS Threads/Tasks    : sys_thread_new -> xTaskCreate()
 *   - Critical Sections   : sys_arch_protect/unprotect -> taskENTER/EXIT_CRITICAL()
 *   - Uptime Clock        : sys_now()   -> xTaskGetTickCount() * portTICK_PERIOD_MS
 * ============================================================================
 */

/* LwIP Core Option & Architecture Headers */
#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/err.h"
#include "arch/sys_arch.h"

/*
 * ----------------------------------------------------------------------------
 * sys_init — Global Architecture Layer Initialization Hook
 * Called during tcpip_init() before any stack tasks or resources are allocated.
 * ----------------------------------------------------------------------------
 */
void sys_init(void)
{
    /* No special initialization required under FreeRTOS */
}

/* ============================================================================
 * LwIP Semaphore Adaptation API (sys_sem_*)
 * ============================================================================
 */

/**
 * @brief Allocates a new counting semaphore.
 *
 * @param[out] sem   Pointer to store allocated semaphore handle.
 * @param[in]  count Initial resource count of the semaphore.
 *
 * @return ERR_OK on success, or ERR_MEM if memory is exhausted.
 */
err_t sys_sem_new(sys_sem_t *sem, u8_t count)
{
    /* Create FreeRTOS counting semaphore with max count 65535 */
    *sem = xSemaphoreCreateCounting(65535, count);

#if (configQUEUE_REGISTRY_SIZE > 0)
    /* Register with kernel queue registry for debugging visualization */
    if (*sem != NULL) {
        vQueueAddToRegistry(*sem, "lwip_sem");
    }
#endif

    return (*sem != NULL) ? ERR_OK : ERR_MEM;
}

/**
 * @brief Deletes a semaphore and frees kernel memory.
 *
 * @param[in,out] sem Pointer to the semaphore handle to delete.
 */
void sys_sem_free(sys_sem_t *sem)
{
    if (sem != NULL && *sem != NULL) {
        vSemaphoreDelete(*sem);
        *sem = NULL; /* Invalidate handle */
    }
}

/**
 * @brief Signals (unblocks) a semaphore.
 *
 * @param[in] sem Pointer to the semaphore handle to signal.
 */
void sys_sem_signal(sys_sem_t *sem)
{
    if (sem != NULL && *sem != NULL) {
        xSemaphoreGive(*sem);
    }
}

/**
 * @brief Waits for a semaphore with optional timeout.
 *
 * @param[in] sem     Pointer to semaphore handle.
 * @param[in] timeout Timeout in milliseconds (0 = wait indefinitely).
 *
 * @return Elapsed wait time in milliseconds, or SYS_ARCH_TIMEOUT if timed out.
 */
u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)
{
    TickType_t start_tick = xTaskGetTickCount();

    if (timeout == 0) {
        /* Block indefinitely until semaphore is signaled */
        if (xSemaphoreTake(*sem, portMAX_DELAY) != pdPASS) {
            return SYS_ARCH_TIMEOUT;
        }
    } else {
        /* Convert requested timeout in ms to FreeRTOS scheduler ticks */
        TickType_t timeout_ticks = pdMS_TO_TICKS(timeout);
        if (timeout_ticks == 0) {
            timeout_ticks = 1; /* Ensure minimum 1 tick wait */
        }
        if (xSemaphoreTake(*sem, timeout_ticks) != pdPASS) {
            return SYS_ARCH_TIMEOUT;
        }
    }

    /* Calculate and return elapsed time in milliseconds */
    return (xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS;
}

/* ============================================================================
 * LwIP Mutex Adaptation API (sys_mutex_*)
 * ============================================================================
 */

/**
 * @brief Allocates a new mutual exclusion lock with priority inheritance.
 *
 * @param[out] mutex Pointer to store allocated mutex handle.
 *
 * @return ERR_OK on success, or ERR_MEM if memory is exhausted.
 */
err_t sys_mutex_new(sys_mutex_t *mutex)
{
    *mutex = xSemaphoreCreateMutex();

#if (configQUEUE_REGISTRY_SIZE > 0)
    if (*mutex != NULL) {
        vQueueAddToRegistry(*mutex, "lwip_mutex");
    }
#endif

    return (*mutex != NULL) ? ERR_OK : ERR_MEM;
}

/**
 * @brief Deletes a mutex lock and clears its handle.
 */
void sys_mutex_free(sys_mutex_t *mutex)
{
    if (mutex != NULL && *mutex != NULL) {
        vSemaphoreDelete(*mutex);
        *mutex = NULL;
    }
}

/**
 * @brief Locks a mutex, blocking indefinitely until acquired.
 */
void sys_mutex_lock(sys_mutex_t *mutex)
{
    if (mutex != NULL && *mutex != NULL) {
        xSemaphoreTake(*mutex, portMAX_DELAY);
    }
}

/**
 * @brief Releases ownership of a held mutex lock.
 */
void sys_mutex_unlock(sys_mutex_t *mutex)
{
    if (mutex != NULL && *mutex != NULL) {
        xSemaphoreGive(*mutex);
    }
}

/* ============================================================================
 * LwIP Mailbox Adaptation API (sys_mbox_*)
 *
 * Mailboxes are thread-safe message queues passing void* pointers between
 * application threads and the core LwIP tcpip_thread daemon.
 * ============================================================================
 */

/**
 * @brief Creates a new mailbox message queue.
 *
 * @param[out] mbox Pointer to store allocated mailbox handle.
 * @param[in]  size Capacity of the mailbox in number of messages (void* pointers).
 *
 * @return ERR_OK on success, or ERR_MEM on allocation failure.
 */
err_t sys_mbox_new(sys_mbox_t *mbox, int size)
{
    /* Create FreeRTOS queue holding 'size' elements of type void* */
    *mbox = xQueueCreate(size, sizeof(void *));

#if (configQUEUE_REGISTRY_SIZE > 0)
    if (*mbox != NULL) {
        vQueueAddToRegistry(*mbox, "lwip_mbox");
    }
#endif

    return (*mbox != NULL) ? ERR_OK : ERR_MEM;
}

/**
 * @brief Deletes a mailbox message queue.
 */
void sys_mbox_free(sys_mbox_t *mbox)
{
    if (mbox != NULL && *mbox != NULL) {
        vQueueDelete(*mbox);
        *mbox = NULL;
    }
}

/**
 * @brief Posts a message to a mailbox, blocking indefinitely if full.
 */
void sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
    xQueueSendToBack(*mbox, &msg, portMAX_DELAY);
}

/**
 * @brief Attempts to post a message without blocking.
 *
 * @return ERR_OK if posted, or ERR_MEM if mailbox queue is currently full.
 */
err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
    return (xQueueSendToBack(*mbox, &msg, 0) == pdPASS) ? ERR_OK : ERR_MEM;
}

/**
 * @brief Fetches a message from a mailbox with timeout support.
 *
 * @param[in]  mbox    Mailbox queue to read from.
 * @param[out] msg     Location to store the retrieved message pointer.
 * @param[in]  timeout Timeout in milliseconds (0 = wait indefinitely).
 *
 * @return Elapsed wait time in milliseconds, or SYS_ARCH_TIMEOUT on timeout.
 */
u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout)
{
    void *dummy;
    if (msg == NULL) {
        msg = &dummy; /* Prevent null-pointer dereference */
    }

    TickType_t start_tick = xTaskGetTickCount();

    if (timeout == 0) {
        /* Block indefinitely until a message arrives */
        if (xQueueReceive(*mbox, msg, portMAX_DELAY) != pdPASS) {
            return SYS_ARCH_TIMEOUT;
        }
    } else {
        TickType_t timeout_ticks = pdMS_TO_TICKS(timeout);
        if (timeout_ticks == 0) {
            timeout_ticks = 1;
        }
        if (xQueueReceive(*mbox, msg, timeout_ticks) != pdPASS) {
            return SYS_ARCH_TIMEOUT;
        }
    }

    /* Return elapsed wait duration in milliseconds */
    return (xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS;
}

/**
 * @brief Attempts to fetch a message from a mailbox without blocking.
 *
 * @return 0 on successful message retrieval, or SYS_MBOX_EMPTY if queue is empty.
 */
u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
    void *dummy;
    if (msg == NULL) {
        msg = &dummy;
    }
    return (xQueueReceive(*mbox, msg, 0) == pdPASS) ? 0 : SYS_MBOX_EMPTY;
}

/* ============================================================================
 * LwIP System Thread API
 * ============================================================================
 */

/**
 * @brief Spawns a new LwIP system task (e.g. tcpip_thread).
 *
 * @param[in] name      Task name string for debugging.
 * @param[in] thread    Task entry point function.
 * @param[in] arg       Parameter passed to thread function.
 * @param[in] stacksize Stack size allocated in words.
 * @param[in] prio      FreeRTOS priority level.
 *
 * @return TaskHandle_t of created task, or NULL on failure.
 */
sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread,
                            void *arg, int stacksize, int prio)
{
    TaskHandle_t xCreatedTask;
    BaseType_t result = xTaskCreate((TaskFunction_t)thread,
                                    name,
                                    stacksize,
                                    arg,
                                    prio,
                                    &xCreatedTask);
    return (result == pdPASS) ? xCreatedTask : NULL;
}

/**
 * @brief Returns system uptime in milliseconds.
 */
u32_t sys_now(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

/* ============================================================================
 * Handle Validity Query & Reset Helper Functions
 * ============================================================================
 */

int sys_sem_valid(sys_sem_t *sem)
{
    return (sem != NULL && *sem != NULL);
}

void sys_sem_set_invalid(sys_sem_t *sem)
{
    if (sem != NULL) {
        *sem = NULL;
    }
}

int sys_mutex_valid(sys_mutex_t *mutex)
{
    return (mutex != NULL && *mutex != NULL);
}

void sys_mutex_set_invalid(sys_mutex_t *mutex)
{
    if (mutex != NULL) {
        *mutex = NULL;
    }
}

int sys_mbox_valid(sys_mbox_t *mbox)
{
    return (mbox != NULL && *mbox != NULL);
}

void sys_mbox_set_invalid(sys_mbox_t *mbox)
{
    if (mbox != NULL) {
        *mbox = NULL;
    }
}

/* ============================================================================
 * Architecture Critical Section Protection API
 * ============================================================================
 */

sys_prot_t sys_arch_protect(void)
{
    taskENTER_CRITICAL();
    return 0;
}

void sys_arch_unprotect(sys_prot_t pval)
{
    (void)pval;
    taskEXIT_CRITICAL();
}
