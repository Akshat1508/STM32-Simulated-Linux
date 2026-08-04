/*
 * posix_shim.h — POSIX Compatibility Layer for FreeRTOS on ARM Cortex-M
 *
 * Provides the public API surface of the POSIX shim that maps standard Linux
 * calls to FreeRTOS primitives. Application code should include this header
 * to access pthread, mutex, semaphore, and delay functions.
 */

#ifndef POSIX_SHIM_H
#define POSIX_SHIM_H

#include <pthread.h>   /* pthread_t, pthread_mutex_t, pthread_attr_t, ... */
#include <unistd.h>    /* useconds_t */

/*
 * sem_t — opaque handle to a FreeRTOS counting semaphore.
 * <semaphore.h> is not available under bare-metal newlib configurations.
 */
typedef void *sem_t;

/* --- POSIX Thread API ----------------------------------------------------- */

int       pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                         void *(*start_routine)(void *), void *arg);
int       pthread_join(pthread_t thread, void **retval);
void      pthread_exit(void *retval);
int       pthread_detach(pthread_t thread);
pthread_t pthread_self(void);

/* --- POSIX Mutex API ------------------------------------------------------ */

int       pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int       pthread_mutex_lock(pthread_mutex_t *mutex);
int       pthread_mutex_unlock(pthread_mutex_t *mutex);
int       pthread_mutex_destroy(pthread_mutex_t *mutex);

/* --- POSIX Delay & Sleep API ---------------------------------------------- */

unsigned int sleep(unsigned int seconds);
int          usleep(useconds_t useconds);

/* --- POSIX Semaphore API -------------------------------------------------- */

int       sem_init(sem_t *sem, int pshared, unsigned int value);
int       sem_wait(sem_t *sem);
int       sem_post(sem_t *sem);
int       sem_destroy(sem_t *sem);

#endif /* POSIX_SHIM_H */
