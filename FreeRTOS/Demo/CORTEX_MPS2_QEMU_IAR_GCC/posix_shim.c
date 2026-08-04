/*
 * posix_shim.c — POSIX Compatibility Layer implementation for FreeRTOS
 *
 * Maps the following POSIX APIs to FreeRTOS primitives:
 *
 *   Threads   : pthread_create, pthread_join, pthread_exit,
 *               pthread_detach, pthread_self
 *   Mutexes   : pthread_mutex_init, pthread_mutex_lock,
 *               pthread_mutex_unlock, pthread_mutex_destroy
 *   Semaphores: sem_init, sem_wait, sem_post, sem_destroy
 *   Timing    : sleep, usleep
 *
 * All implementations are self-contained in this file.  The public API is
 * declared in posix_shim.h.  Internal helpers and the posix_thread_t struct
 * are kept file-scoped (static) and are not exposed to application code.
 */

#include "posix_shim.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* =========================================================================
 * Internal thread registry
 * =========================================================================
 *
 * Every POSIX thread is tracked via a singly-linked list of posix_thread_t
 * nodes allocated from the FreeRTOS heap.  The list is protected with
 * taskENTER/EXIT_CRITICAL() which maps to a priority-mask critical section
 * on Cortex-M.
 */

typedef struct posix_thread {
    TaskHandle_t       xTask;          /* FreeRTOS task handle               */
    void              *(*start_routine)(void *); /* user thread function      */
    void              *arg;            /* argument passed to start_routine    */
    void              *retval;         /* value passed to pthread_exit()      */
    SemaphoreHandle_t  join_sem;       /* binary semaphore for pthread_join() */
    volatile int       detached;       /* 1 when thread is detached           */
    struct posix_thread *next;         /* intrusive list link                 */
} posix_thread_t;

/* Head of the global thread registry.  Access must be guarded by a critical
 * section (taskENTER_CRITICAL / taskEXIT_CRITICAL). */
static posix_thread_t *g_thread_list = NULL;

/* -------------------------------------------------------------------------
 * add_thread() — prepend t to the registry (O(1)).
 * Called with the scheduler suspended so the task cannot preempt itself
 * before it is registered.
 * ------------------------------------------------------------------------- */
static void add_thread(posix_thread_t *t)
{
    taskENTER_CRITICAL();
    t->next       = g_thread_list;
    g_thread_list = t;
    taskEXIT_CRITICAL();
}

/* -------------------------------------------------------------------------
 * find_thread() — locate the registry node matching xTask.
 * Returns NULL if not found.
 *
 * NOTE: The returned pointer is valid only while the caller holds a
 * reference to it inside a critical section or before it calls
 * vTaskDelete / remove_thread on the same node.
 * ------------------------------------------------------------------------- */
static posix_thread_t *find_thread(TaskHandle_t xTask)
{
    taskENTER_CRITICAL();
    posix_thread_t *curr = g_thread_list;
    while (curr != NULL) {
        if (curr->xTask == xTask) {
            break;
        }
        curr = curr->next;
    }
    taskEXIT_CRITICAL();
    return curr;
}

/* -------------------------------------------------------------------------
 * remove_thread() — unlink t from the registry (O(n)).
 * ------------------------------------------------------------------------- */
static void remove_thread(posix_thread_t *t)
{
    taskENTER_CRITICAL();
    posix_thread_t **curr = &g_thread_list;
    while (*curr != NULL) {
        if (*curr == t) {
            *curr = t->next;
            break;
        }
        curr = &((*curr)->next);
    }
    taskEXIT_CRITICAL();
}

/* -------------------------------------------------------------------------
 * posix_thread_wrapper() — FreeRTOS task function used for every POSIX thread.
 *
 * Invokes the user's start_routine and then calls pthread_exit() with the
 * return value so that join/detach cleanup always runs through a single path.
 * ------------------------------------------------------------------------- */
static void posix_thread_wrapper(void *pvParameters)
{
    posix_thread_t *t = (posix_thread_t *)pvParameters;
    void *retval = t->start_routine(t->arg);
    pthread_exit(retval);
}

/* =========================================================================
 * POSIX Thread API
 * ========================================================================= */

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg)
{
    (void)attr; /* thread attributes not yet implemented */

    posix_thread_t *t = pvPortMalloc(sizeof(posix_thread_t));
    if (t == NULL) {
        return -1;
    }

    t->start_routine = start_routine;
    t->arg           = arg;
    t->retval        = NULL;
    t->detached      = 0;
    t->join_sem      = xSemaphoreCreateBinary();
    if (t->join_sem == NULL) {
        vPortFree(t);
        return -1;
    }

    /* Suspend the scheduler so the new task cannot run and call
     * find_thread() before we have registered it in g_thread_list. */
    vTaskSuspendAll();

    BaseType_t result = xTaskCreate(posix_thread_wrapper,
                                    "POSIX_Thread",
                                    1024,
                                    t,
                                    tskIDLE_PRIORITY + 1,
                                    &(t->xTask));
    if (result != pdPASS) {
        xTaskResumeAll();
        vSemaphoreDelete(t->join_sem);
        vPortFree(t);
        return -1;
    }

    add_thread(t);
    *thread = (pthread_t)(uintptr_t)t;

    xTaskResumeAll();
    return 0;
}

int pthread_join(pthread_t thread, void **retval)
{
    posix_thread_t *t = (posix_thread_t *)(uintptr_t)thread;
    if (t == NULL) {
        return -1;
    }

    /* Block until the target thread signals completion via join_sem. */
    if (xSemaphoreTake(t->join_sem, portMAX_DELAY) != pdPASS) {
        return -1;
    }

    if (retval != NULL) {
        *retval = t->retval;
    }

    remove_thread(t);
    vSemaphoreDelete(t->join_sem);
    vPortFree(t);
    return 0;
}

void pthread_exit(void *retval)
{
    posix_thread_t *t = find_thread(xTaskGetCurrentTaskHandle());
    if (t != NULL) {
        t->retval = retval;
        if (t->detached) {
            /* Detached threads self-clean: no joiner will free resources. */
            remove_thread(t);
            vSemaphoreDelete(t->join_sem);
            vPortFree(t);
        } else {
            /* Signal any waiting pthread_join(). */
            xSemaphoreGive(t->join_sem);
        }
    }
    vTaskDelete(NULL);
    while (1); /* unreachable; silences compiler warnings */
}

int pthread_detach(pthread_t thread)
{
    posix_thread_t *t = (posix_thread_t *)(uintptr_t)thread;
    if (t == NULL) {
        return -1;
    }
    t->detached = 1;
    return 0;
}

pthread_t pthread_self(void)
{
    posix_thread_t *t = find_thread(xTaskGetCurrentTaskHandle());
    return (pthread_t)(uintptr_t)t;
}

/* =========================================================================
 * POSIX Mutex API
 *
 * pthread_mutex_t (from <pthread.h>) is stored as a uintptr_t.  We cast it
 * to hold a FreeRTOS SemaphoreHandle_t (a pointer-sized value on Cortex-M).
 * ========================================================================= */

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    (void)attr;
    if (mutex == NULL) {
        return -1;
    }
    SemaphoreHandle_t xMutex = xSemaphoreCreateMutex();
    if (xMutex == NULL) {
        return -1;
    }
    *mutex = (pthread_mutex_t)(uintptr_t)xMutex;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    if (mutex == NULL) {
        return -1;
    }
    SemaphoreHandle_t xMutex = (SemaphoreHandle_t)(uintptr_t)*mutex;
    if (xMutex == NULL) {
        return -1;
    }
    return (xSemaphoreTake(xMutex, portMAX_DELAY) == pdPASS) ? 0 : -1;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if (mutex == NULL) {
        return -1;
    }
    SemaphoreHandle_t xMutex = (SemaphoreHandle_t)(uintptr_t)*mutex;
    if (xMutex == NULL) {
        return -1;
    }
    return (xSemaphoreGive(xMutex) == pdPASS) ? 0 : -1;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    if (mutex == NULL) {
        return -1;
    }
    SemaphoreHandle_t xMutex = (SemaphoreHandle_t)(uintptr_t)*mutex;
    if (xMutex != NULL) {
        vSemaphoreDelete(xMutex);
        *mutex = 0;
    }
    return 0;
}

/* =========================================================================
 * POSIX Timing API
 * ========================================================================= */

unsigned int sleep(unsigned int seconds)
{
    vTaskDelay(pdMS_TO_TICKS(seconds * 1000UL));
    return 0;
}

int usleep(useconds_t useconds)
{
    TickType_t ticks = pdMS_TO_TICKS(useconds / 1000UL);
    if (ticks == 0 && useconds > 0) {
        ticks = 1; /* guarantee at least one tick for any non-zero delay */
    }
    vTaskDelay(ticks);
    return 0;
}

/* =========================================================================
 * POSIX Semaphore API
 *
 * sem_t (from posix_shim.h) is stored as void* holding a SemaphoreHandle_t.
 * ========================================================================= */

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
    (void)pshared; /* no inter-process sharing on bare-metal */
    if (sem == NULL) {
        return -1;
    }
    SemaphoreHandle_t xSem = xSemaphoreCreateCounting(65535, value);
    if (xSem == NULL) {
        return -1;
    }
    *sem = (sem_t)xSem;
    return 0;
}

int sem_wait(sem_t *sem)
{
    if (sem == NULL) {
        return -1;
    }
    SemaphoreHandle_t xSem = (SemaphoreHandle_t)*sem;
    if (xSem == NULL) {
        return -1;
    }
    return (xSemaphoreTake(xSem, portMAX_DELAY) == pdPASS) ? 0 : -1;
}

int sem_post(sem_t *sem)
{
    if (sem == NULL) {
        return -1;
    }
    SemaphoreHandle_t xSem = (SemaphoreHandle_t)*sem;
    if (xSem == NULL) {
        return -1;
    }
    return (xSemaphoreGive(xSem) == pdPASS) ? 0 : -1;
}

int sem_destroy(sem_t *sem)
{
    if (sem == NULL) {
        return -1;
    }
    SemaphoreHandle_t xSem = (SemaphoreHandle_t)*sem;
    if (xSem != NULL) {
        vSemaphoreDelete(xSem);
        *sem = NULL;
    }
    return 0;
}
