#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/err.h"
#include "arch/sys_arch.h"

void sys_init(void) {
    /* No initialization needed for FreeRTOS environment */
}

err_t sys_sem_new(sys_sem_t *sem, u8_t count) {
    *sem = xSemaphoreCreateCounting(65535, count);
#if (configQUEUE_REGISTRY_SIZE > 0)
    if (*sem != NULL) {
        vQueueAddToRegistry(*sem, "lwip_sem");
    }
#endif
    return (*sem != NULL) ? ERR_OK : ERR_MEM;
}

void sys_sem_free(sys_sem_t *sem) {
    if (sem != NULL && *sem != NULL) {
        vSemaphoreDelete(*sem);
        *sem = NULL;
    }
}

void sys_sem_signal(sys_sem_t *sem) {
    if (sem != NULL && *sem != NULL) {
        xSemaphoreGive(*sem);
    }
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout) {
    TickType_t start_tick = xTaskGetTickCount();
    if (timeout == 0) {
        if (xSemaphoreTake(*sem, portMAX_DELAY) != pdPASS) {
            return SYS_ARCH_TIMEOUT;
        }
    } else {
        TickType_t timeout_ticks = pdMS_TO_TICKS(timeout);
        if (timeout_ticks == 0) {
            timeout_ticks = 1;
        }
        if (xSemaphoreTake(*sem, timeout_ticks) != pdPASS) {
            return SYS_ARCH_TIMEOUT;
        }
    }
    return (xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS;
}

err_t sys_mutex_new(sys_mutex_t *mutex) {
    *mutex = xSemaphoreCreateMutex();
#if (configQUEUE_REGISTRY_SIZE > 0)
    if (*mutex != NULL) {
        vQueueAddToRegistry(*mutex, "lwip_mutex");
    }
#endif
    return (*mutex != NULL) ? ERR_OK : ERR_MEM;
}

void sys_mutex_free(sys_mutex_t *mutex) {
    if (mutex != NULL && *mutex != NULL) {
        vSemaphoreDelete(*mutex);
        *mutex = NULL;
    }
}

void sys_mutex_lock(sys_mutex_t *mutex) {
    if (mutex != NULL && *mutex != NULL) {
        xSemaphoreTake(*mutex, portMAX_DELAY);
    }
}

void sys_mutex_unlock(sys_mutex_t *mutex) {
    if (mutex != NULL && *mutex != NULL) {
        xSemaphoreGive(*mutex);
    }
}

err_t sys_mbox_new(sys_mbox_t *mbox, int size) {
    *mbox = xQueueCreate(size, sizeof(void *));
#if (configQUEUE_REGISTRY_SIZE > 0)
    if (*mbox != NULL) {
        vQueueAddToRegistry(*mbox, "lwip_mbox");
    }
#endif
    return (*mbox != NULL) ? ERR_OK : ERR_MEM;
}

void sys_mbox_free(sys_mbox_t *mbox) {
    if (mbox != NULL && *mbox != NULL) {
        vQueueDelete(*mbox);
        *mbox = NULL;
    }
}

void sys_mbox_post(sys_mbox_t *mbox, void *msg) {
    xQueueSendToBack(*mbox, &msg, portMAX_DELAY);
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg) {
    return (xQueueSendToBack(*mbox, &msg, 0) == pdPASS) ? ERR_OK : ERR_MEM;
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout) {
    void *dummy;
    if (msg == NULL) {
        msg = &dummy;
    }
    TickType_t start_tick = xTaskGetTickCount();
    if (timeout == 0) {
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
    return (xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS;
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg) {
    void *dummy;
    if (msg == NULL) {
        msg = &dummy;
    }
    return (xQueueReceive(*mbox, msg, 0) == pdPASS) ? 0 : SYS_MBOX_EMPTY;
}

sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg, int stacksize, int prio) {
    TaskHandle_t xCreatedTask;
    BaseType_t result = xTaskCreate((TaskFunction_t)thread, name, stacksize, arg, prio, &xCreatedTask);
    return (result == pdPASS) ? xCreatedTask : NULL;
}

u32_t sys_now(void) {
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

int sys_sem_valid(sys_sem_t *sem) {
    return (sem != NULL && *sem != NULL);
}

void sys_sem_set_invalid(sys_sem_t *sem) {
    if (sem != NULL) {
        *sem = NULL;
    }
}

int sys_mutex_valid(sys_mutex_t *mutex) {
    return (mutex != NULL && *mutex != NULL);
}

void sys_mutex_set_invalid(sys_mutex_t *mutex) {
    if (mutex != NULL) {
        *mutex = NULL;
    }
}

int sys_mbox_valid(sys_mbox_t *mbox) {
    return (mbox != NULL && *mbox != NULL);
}

void sys_mbox_set_invalid(sys_mbox_t *mbox) {
    if (mbox != NULL) {
        *mbox = NULL;
    }
}

sys_prot_t sys_arch_protect(void) {
    taskENTER_CRITICAL();
    return 0;
}

void sys_arch_unprotect(sys_prot_t pval) {
    (void)pval;
    taskEXIT_CRITICAL();
}
