#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"

/* --- THE POSIX SHIM --- */
typedef TaskHandle_t pthread_t;
struct thread_args { void *(*start_routine) (void *); void *arg; };

void posix_thread_wrapper(void *pvParameters) {
    struct thread_args *args = (struct thread_args *)pvParameters;
    args->start_routine(args->arg);
    vTaskDelete(NULL);
}

int pthread_create(pthread_t *thread, const void *attr, void *(*start_routine) (void *), void *arg) {
    static struct thread_args t_args;
    t_args.start_routine = start_routine;
    t_args.arg = arg;
    BaseType_t result = xTaskCreate(posix_thread_wrapper, "POSIX_Thread", 1024, &t_args, tskIDLE_PRIORITY + 1, thread);
    return (result == pdPASS) ? 0 : -1;
}

/* --- THE LINUX APPLICATION --- */
void* my_linux_application(void* arg) {
    int counter = 0;
    while(1) {
        printf("[Linux App] Hello from POSIX thread! Counter: %d\n", counter++);
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
    return NULL;
}

/* --- MAIN ENTRY POINT --- */
// The original main.c will automatically call this function after hardware is safely initialized!
void main_blinky( void ) {
    printf("--- Booting Simulated Linux Environment ---\n");
    
    pthread_t my_thread;
    if (pthread_create(&my_thread, NULL, my_linux_application, NULL) == 0) {
        printf("Thread translated successfully.\n");
    }
    
    // Start the RTOS scheduler
    vTaskStartScheduler();
}