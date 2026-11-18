/*
 * ============================================================================
 * File: posix_shim.h
 * Description: Public Interface of the POSIX Compatibility Layer for FreeRTOS
 * Target Platform: ARM Cortex-M (QEMU MPS2 AN385 / Cortex-M3 / STM32)
 *
 * This header exposes standard POSIX-compliant API signatures that bridge
 * multi-threaded Linux/UNIX applications directly onto bare-metal FreeRTOS
 * real-time kernel primitives without requiring an MMU or a heavy OS image.
 * ============================================================================
 */

#ifndef POSIX_SHIM_H
#define POSIX_SHIM_H

/* Standard POSIX and C library type headers */
#include <pthread.h>   /* Provides pthread_t, pthread_mutex_t, pthread_cond_t, etc. */
#include <unistd.h>    /* Provides useconds_t and standard timing types */
#include <errno.h>     /* Provides ETIMEDOUT and standard error codes */
#include <time.h>      /* Provides struct timespec for timed operations */

/*
 * ----------------------------------------------------------------------------
 * Type Definition: sem_t
 *
 * Standard bare-metal newlib distributions (such as arm-none-eabi-gcc) do not
 * ship with <semaphore.h>. We define sem_t as an opaque pointer handle
 * representing an underlying FreeRTOS counting semaphore (SemaphoreHandle_t).
 * ----------------------------------------------------------------------------
 */
typedef void *sem_t;

/* ============================================================================
 * POSIX Thread Lifecycle Management API
 * ============================================================================
 */

/**
 * @brief Spawns a new POSIX thread backed by a native FreeRTOS task.
 *
 * Allocates a dynamic thread tracking control block (posix_thread_t), creates
 * a binary completion semaphore for join synchronization, registers the thread
 * in the global task list, and invokes FreeRTOS xTaskCreate().
 *
 * @param[out] thread        Pointer to store the resulting opaque thread handle.
 * @param[in]  attr          Thread creation attributes (stack size, priority).
 *                           Pass NULL to use default attributes (1024 words stack).
 * @param[in]  start_routine Function pointer to the thread entry routine.
 * @param[in]  arg           Single arbitrary argument passed to start_routine.
 *
 * @return 0 on successful thread creation, or -1 if memory/task creation fails.
 */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg);

/**
 * @brief Suspends the calling thread until the targeted thread terminates.
 *
 * Blocks on the target thread's binary join semaphore. Once unblocked, extracts
 * the exit status pointer, unlinks the thread from the global registry, deletes
 * synchronization semaphores, and frees the wrapper metadata from the heap.
 *
 * @param[in]  thread Target thread handle to await completion of.
 * @param[out] retval Pointer to store the void* return value from pthread_exit().
 *                    Pass NULL if the exit value is not required.
 *
 * @return 0 on success, or -1 if the target thread handle is invalid or detached.
 */
int pthread_join(pthread_t thread, void **retval);

/**
 * @brief Terminates the calling POSIX thread and saves its exit status.
 *
 * Retrieves the executing thread's metadata. If joinable, signals the join
 * semaphore and enters deletion; if detached, performs immediate self-reclamation
 * before calling FreeRTOS vTaskDelete(NULL).
 *
 * @param[in] retval Exit return value pointer made available to pthread_join().
 *
 * @note This function never returns to its caller.
 */
void pthread_exit(void *retval);

/**
 * @brief Marks the target thread as detached for automatic resource reclamation.
 *
 * A detached thread automatically cleans up its internal metadata and FreeRTOS
 * task resources upon calling pthread_exit(), without requiring another thread
 * to invoke pthread_join().
 *
 * @param[in] thread Target thread handle to detach.
 *
 * @return 0 on success, or -1 if thread handle is invalid.
 */
int pthread_detach(pthread_t thread);

/**
 * @brief Returns the POSIX thread handle of the currently executing task.
 *
 * Queries the active FreeRTOS task handle (xTaskGetCurrentTaskHandle()) and
 * matches it against the global thread registry inside a critical section.
 *
 * @return Opaque pthread_t handle of the calling thread, or NULL if untracked.
 */
pthread_t pthread_self(void);

/* ============================================================================
 * POSIX Mutual Exclusion (Mutex) Synchronization API
 * ============================================================================
 */

/**
 * @brief Initializes a POSIX mutual exclusion lock.
 *
 * Allocates a FreeRTOS mutex semaphore featuring priority inheritance to
 * mitigate priority inversion anomalies. Stores the handle in *mutex.
 *
 * @param[out] mutex Pointer to the pthread_mutex_t variable to initialize.
 * @param[in]  attr  Mutex attributes pointer (currently unused; pass NULL).
 *
 * @return 0 on successful initialization, or -1 on allocation failure.
 */
int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);

/**
 * @brief Acquires ownership of a POSIX mutual exclusion lock.
 *
 * Blocks indefinitely until the lock becomes available. If another thread
 * holds the lock, the caller is transitioned into the FreeRTOS Blocked state.
 *
 * @param[in,out] mutex Pointer to the initialized mutex lock to acquire.
 *
 * @return 0 on successful lock acquisition, or -1 on invalid handle error.
 */
int pthread_mutex_lock(pthread_mutex_t *mutex);

/**
 * @brief Releases ownership of a previously acquired POSIX mutex lock.
 *
 * Gives back the underlying FreeRTOS mutex semaphore, unblocking the highest
 * priority task currently waiting on the lock.
 *
 * @param[in,out] mutex Pointer to the held mutex lock to release.
 *
 * @return 0 on success, or -1 if lock was invalid or not owned.
 */
int pthread_mutex_unlock(pthread_mutex_t *mutex);

/**
 * @brief Destroys a POSIX mutex lock and reclaims kernel semaphore memory.
 *
 * Deletes the FreeRTOS mutex semaphore handle and resets the handle value to 0.
 *
 * @param[in,out] mutex Pointer to the mutex lock to destroy.
 *
 * @return 0 on success, or -1 if pointer is NULL.
 */
int pthread_mutex_destroy(pthread_mutex_t *mutex);

/* ============================================================================
 * POSIX Condition Variable Synchronization API
 * ============================================================================
 */

/**
 * @brief Initializes a POSIX condition variable.
 *
 * Allocates an internal condition variable control structure backed by
 * FreeRTOS semaphores and a waiter queue. Enables threads to efficiently
 * wait for state changes without busy polling.
 *
 * @param[out] cond Pointer to the pthread_cond_t variable to initialize.
 * @param[in]  attr Condition variable attributes (currently unused; pass NULL).
 *
 * @return 0 on successful initialization, or -1 on allocation failure.
 */
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);

/**
 * @brief Atomically releases the mutex and blocks on the condition variable.
 *
 * The calling thread atomically releases the associated mutex and enters a
 * blocked state on the condition variable's internal waiter queue. When
 * signaled (via pthread_cond_signal or pthread_cond_broadcast), the thread
 * re-acquires the mutex before returning.
 *
 * @param[in,out] cond  Pointer to the initialized condition variable.
 * @param[in,out] mutex Pointer to the mutex currently held by the caller.
 *
 * @return 0 on success, or -1 on error.
 *
 * @note The mutex must be locked by the calling thread prior to invocation.
 */
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);

/**
 * @brief Wakes one thread waiting on the condition variable.
 *
 * Unblocks the highest-priority (or longest-waiting) thread currently
 * blocked in pthread_cond_wait or pthread_cond_timedwait on this
 * condition variable.
 *
 * @param[in,out] cond Pointer to the condition variable to signal.
 *
 * @return 0 on success, or -1 on error.
 */
int pthread_cond_signal(pthread_cond_t *cond);

/**
 * @brief Wakes all threads waiting on the condition variable.
 *
 * Unblocks every thread currently blocked in pthread_cond_wait or
 * pthread_cond_timedwait on this condition variable.
 *
 * @param[in,out] cond Pointer to the condition variable to broadcast.
 *
 * @return 0 on success, or -1 on error.
 */
int pthread_cond_broadcast(pthread_cond_t *cond);

/**
 * @brief Destroys a condition variable and reclaims its resources.
 *
 * Frees the internal waiter queue structures, internal lock semaphore,
 * and the condition variable control block. Must not be called while
 * any thread is blocked on the condition variable.
 *
 * @param[in,out] cond Pointer to the condition variable to destroy.
 *
 * @return 0 on success, or -1 on error.
 */
int pthread_cond_destroy(pthread_cond_t *cond);

/* ============================================================================
 * POSIX Timing, Delays & Scheduler Yield API
 * ============================================================================
 */

/**
 * @brief Suspends execution of the calling thread for a specified number of seconds.
 *
 * Translates seconds into system timer ticks (configTICK_RATE_HZ) and invokes
 * FreeRTOS vTaskDelay(). The task consumes 0% CPU cycles while blocked.
 *
 * @param[in] seconds Duration in seconds to delay thread execution.
 *
 * @return Always returns 0 upon successful delay completion.
 */
unsigned int sleep(unsigned int seconds);

/**
 * @brief Suspends execution of the calling thread for a specified number of microseconds.
 *
 * Converts microseconds into kernel ticks. Guarantees at least a 1-tick delay
 * for any non-zero microsecond argument to ensure thread yielding.
 *
 * @param[in] useconds Duration in microseconds to delay thread execution.
 *
 * @return Always returns 0 upon successful delay completion.
 */
int usleep(useconds_t useconds);

/* ============================================================================
 * POSIX Counting Semaphore Synchronization API
 * ============================================================================
 */

/**
 * @brief Initializes an unnamed counting semaphore.
 *
 * Creates a FreeRTOS counting semaphore initialized with the given count
 * and a maximum capacity of 65535.
 *
 * @param[out] sem     Pointer to the sem_t handle to initialize.
 * @param[in]  pshared Process sharing flag (ignored on bare-metal unified memory).
 * @param[in]  value   Initial resource count value.
 *
 * @return 0 on success, or -1 on allocation failure.
 */
int sem_init(sem_t *sem, int pshared, unsigned int value);

/**
 * @brief Decrements (locks) the counting semaphore.
 *
 * Decrements the semaphore count. If the count is 0, blocks the calling task
 * indefinitely until another thread executes sem_post().
 *
 * @param[in,out] sem Pointer to the counting semaphore.
 *
 * @return 0 on successful decrement, or -1 on error.
 */
int sem_wait(sem_t *sem);

/**
 * @brief Increments (unlocks) the counting semaphore.
 *
 * Increments the semaphore count and wakes up the highest-priority task
 * blocked in sem_wait().
 *
 * @param[in,out] sem Pointer to the counting semaphore.
 *
 * @return 0 on successful increment, or -1 on error.
 */
int sem_post(sem_t *sem);

/**
 * @brief Destroys a counting semaphore and reclaims its resources.
 *
 * Deletes the FreeRTOS counting semaphore and sets *sem to NULL.
 *
 * @param[in,out] sem Pointer to the counting semaphore to destroy.
 *
 * @return 0 on success, or -1 on error.
 */
int sem_destroy(sem_t *sem);

#endif /* POSIX_SHIM_H */
