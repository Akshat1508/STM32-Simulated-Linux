#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* --- POSIX SEMAPHORE TYPE & PROTOTYPES --- */
typedef void* sem_t;

int sem_init(sem_t *sem, int pshared, unsigned int value);
int sem_wait(sem_t *sem);
int sem_post(sem_t *sem);
int sem_destroy(sem_t *sem);

/* --- THE POSIX SHIM LAYER IMPLEMENTATION --- */

typedef struct posix_thread {
    TaskHandle_t xTask;
    void *(*start_routine)(void *);
    void *arg;
    void *retval;
    SemaphoreHandle_t join_sem;
    volatile int detached;
    struct posix_thread *next;
} posix_thread_t;

static posix_thread_t *g_thread_list = NULL;

static void add_thread(posix_thread_t *t) {
    taskENTER_CRITICAL();
    t->next = g_thread_list;
    g_thread_list = t;
    taskEXIT_CRITICAL();
}

static posix_thread_t *find_thread(TaskHandle_t xTask) {
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

static void remove_thread(posix_thread_t *t) {
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

void posix_thread_wrapper(void *pvParameters) {
    posix_thread_t *t = (posix_thread_t *)pvParameters;
    void *retval = t->start_routine(t->arg);
    pthread_exit(retval);
}

int pthread_create(pthread_t *thread, const void *attr, void *(*start_routine) (void *), void *arg) {
    (void)attr;
    posix_thread_t *t = pvPortMalloc(sizeof(posix_thread_t));
    if (t == NULL) {
        return -1;
    }
    t->start_routine = start_routine;
    t->arg = arg;
    t->retval = NULL;
    t->detached = 0;
    t->join_sem = xSemaphoreCreateBinary();
    if (t->join_sem == NULL) {
        vPortFree(t);
        return -1;
    }
    
    // Suspend scheduler to ensure task handle is registered before the task runs
    vTaskSuspendAll();
    
    BaseType_t result = xTaskCreate(posix_thread_wrapper, "POSIX_Thread", 1024, t, tskIDLE_PRIORITY + 1, &(t->xTask));
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

int pthread_join(pthread_t thread, void **retval) {
    posix_thread_t *t = (posix_thread_t *)(uintptr_t)thread;
    if (t == NULL) {
        return -1;
    }
    
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

void pthread_exit(void *retval) {
    posix_thread_t *t = find_thread(xTaskGetCurrentTaskHandle());
    if (t != NULL) {
        t->retval = retval;
        if (t->detached) {
            remove_thread(t);
            vSemaphoreDelete(t->join_sem);
            vPortFree(t);
        } else {
            xSemaphoreGive(t->join_sem);
        }
    }
    vTaskDelete(NULL);
    while (1); // Should never reach here
}

int pthread_detach(pthread_t thread) {
    posix_thread_t *t = (posix_thread_t *)(uintptr_t)thread;
    if (t == NULL) {
        return -1;
    }
    t->detached = 1;
    return 0;
}

pthread_t pthread_self(void) {
    posix_thread_t *t = find_thread(xTaskGetCurrentTaskHandle());
    return (pthread_t)(uintptr_t)t;
}

/* --- MUTEX SHIMS --- */

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
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

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (mutex == NULL) {
        return -1;
    }
    SemaphoreHandle_t xMutex = (SemaphoreHandle_t)(uintptr_t)*mutex;
    if (xMutex == NULL) {
        return -1;
    }
    BaseType_t result = xSemaphoreTake(xMutex, portMAX_DELAY);
    return (result == pdPASS) ? 0 : -1;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if (mutex == NULL) {
        return -1;
    }
    SemaphoreHandle_t xMutex = (SemaphoreHandle_t)(uintptr_t)*mutex;
    if (xMutex == NULL) {
        return -1;
    }
    BaseType_t result = xSemaphoreGive(xMutex);
    return (result == pdPASS) ? 0 : -1;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
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

/* --- SLEEP/DELAY SHIMS --- */

unsigned int sleep(unsigned int seconds) {
    vTaskDelay(pdMS_TO_TICKS(seconds * 1000));
    return 0;
}

int usleep(useconds_t useconds) {
    TickType_t ticks = pdMS_TO_TICKS(useconds / 1000);
    if (ticks == 0 && useconds > 0) {
        ticks = 1;
    }
    vTaskDelay(ticks);
    return 0;
}

/* --- SEMAPHORE SHIMS --- */

int sem_init(sem_t *sem, int pshared, unsigned int value) {
    (void)pshared;
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

int sem_wait(sem_t *sem) {
    if (sem == NULL) {
        return -1;
    }
    SemaphoreHandle_t xSem = (SemaphoreHandle_t)*sem;
    if (xSem == NULL) {
        return -1;
    }
    BaseType_t result = xSemaphoreTake(xSem, portMAX_DELAY);
    return (result == pdPASS) ? 0 : -1;
}

int sem_post(sem_t *sem) {
    if (sem == NULL) {
        return -1;
    }
    SemaphoreHandle_t xSem = (SemaphoreHandle_t)*sem;
    if (xSem == NULL) {
        return -1;
    }
    BaseType_t result = xSemaphoreGive(xSem);
    return (result == pdPASS) ? 0 : -1;
}

int sem_destroy(sem_t *sem) {
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

/* --- VERIFICATION & DEMO APPLICATION --- */

static int g_shared_counter = 0;
static pthread_mutex_t g_counter_mutex;
static sem_t g_job_semaphore;

typedef struct {
    int thread_id;
    int iterations;
} thread_config_t;

void* worker_thread_mutex(void* arg) {
    thread_config_t *config = (thread_config_t *)arg;
    printf("[Worker %d] Started. Incrementing counter %d times...\n", config->thread_id, config->iterations);
    
    for (int i = 0; i < config->iterations; i++) {
        pthread_mutex_lock(&g_counter_mutex);
        
        int temp = g_shared_counter;
        // Yield for 50ms to simulate work and increase potential race condition window
        usleep(50000);
        g_shared_counter = temp + 1;
        
        pthread_mutex_unlock(&g_counter_mutex);
        
        // Brief sleep between increments
        usleep(10000);
    }
    
    printf("[Worker %d] Finished.\n", config->thread_id);
    int id = config->thread_id;
    vPortFree(config);
    return (void*)(uintptr_t)id;
}

void* worker_thread_semaphore(void* arg) {
    (void)arg;
    printf("[Sem Worker] Waiting for semaphore...\n");
    sem_wait(&g_job_semaphore);
    printf("[Sem Worker] Semaphore received! Running task...\n");
    sleep(1);
    printf("[Sem Worker] Task completed. Exiting.\n");
    return NULL;
}

void* main_posix_app(void* arg) {
    (void)arg;
    
    // Initialize mutex
    if (pthread_mutex_init(&g_counter_mutex, NULL) != 0) {
        printf("Failed to initialize mutex\n");
        return NULL;
    }
    
    // Initialize semaphore
    if (sem_init(&g_job_semaphore, 0, 0) != 0) {
        printf("Failed to initialize semaphore\n");
        pthread_mutex_destroy(&g_counter_mutex);
        return NULL;
    }
    
    pthread_t thread1, thread2, sem_thread;
    
    // Allocate config for thread 1
    thread_config_t *config1 = pvPortMalloc(sizeof(thread_config_t));
    if (config1 != NULL) {
        config1->thread_id = 1;
        config1->iterations = 5;
    }
    
    // Allocate config for thread 2
    thread_config_t *config2 = pvPortMalloc(sizeof(thread_config_t));
    if (config2 != NULL) {
        config2->thread_id = 2;
        config2->iterations = 5;
    }
    
    if (pthread_create(&thread1, NULL, worker_thread_mutex, config1) != 0) {
        printf("Failed to create Worker 1\n");
    }
    
    if (pthread_create(&thread2, NULL, worker_thread_mutex, config2) != 0) {
        printf("Failed to create Worker 2\n");
    }
    
    if (pthread_create(&sem_thread, NULL, worker_thread_semaphore, NULL) != 0) {
        printf("Failed to create Semaphore Worker\n");
    }
    
    // Let workers run for a bit, then signal the semaphore worker
    sleep(2);
    printf("[Main] Posting to semaphore...\n");
    sem_post(&g_job_semaphore);
    
    // Join threads and check exit status
    void *status1 = NULL;
    void *status2 = NULL;
    void *status_sem = NULL;
    
    printf("[Main] Joining Worker 1...\n");
    pthread_join(thread1, &status1);
    printf("[Main] Worker 1 joined with status: %d\n", (int)(uintptr_t)status1);
    
    printf("[Main] Joining Worker 2...\n");
    pthread_join(thread2, &status2);
    printf("[Main] Worker 2 joined with status: %d\n", (int)(uintptr_t)status2);
    
    printf("[Main] Joining Semaphore Worker...\n");
    pthread_join(sem_thread, &status_sem);
    printf("[Main] Semaphore Worker joined.\n");
    
    printf("[Main] Final shared counter value: %d (Expected: 10)\n", g_shared_counter);
    
    pthread_mutex_destroy(&g_counter_mutex);
    sem_destroy(&g_job_semaphore);
    
    printf("--- Simulated Linux Environment Terminated Successfully ---\n");
    return NULL;
}

/* --- MAIN ENTRY POINT (called by main.c) --- */
void main_blinky(void) {
    printf("--- Booting Simulated Linux Environment ---\n");
    
    pthread_t main_thread;
    if (pthread_create(&main_thread, NULL, main_posix_app, NULL) == 0) {
        pthread_detach(main_thread);
    } else {
        printf("Failed to create main POSIX app thread\n");
    }
    
    // Start the RTOS scheduler
    vTaskStartScheduler();
}