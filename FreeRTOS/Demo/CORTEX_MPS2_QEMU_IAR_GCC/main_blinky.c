#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

#include "posix_shim.h"      /* pthread_*, sem_*, sleep, usleep */

#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"

err_t ethernetif_init(struct netif *netif);

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
        /* Yield for 50 ms to simulate work and widen the race-condition window */
        usleep(50000);
        g_shared_counter = temp + 1;

        pthread_mutex_unlock(&g_counter_mutex);

        usleep(10000); /* brief pause between increments */
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

void* web_server_thread(void* arg) {
    (void)arg;

    /* Initialise LwIP core stack */
    tcpip_init(NULL, NULL);

    ip_addr_t ipaddr, netmask, gw;
    IP4_ADDR(&ipaddr, 10, 0, 2, 15);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 10, 0, 2, 2);

    static struct netif main_netif;
    netif_add(&main_netif, &ipaddr, &netmask, &gw, NULL, ethernetif_init, tcpip_input);
    netif_set_default(&main_netif);
    netif_set_up(&main_netif);

    printf("[Web Server] LwIP Initialized. IP address: 10.0.2.15\n");

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        printf("[Web Server] Failed to create socket\n");
        return NULL;
    }

    struct sockaddr_in address;
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(80);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        printf("[Web Server] Bind failed\n");
        close(server_fd);
        return NULL;
    }

    if (listen(server_fd, 5) < 0) {
        printf("[Web Server] Listen failed\n");
        close(server_fd);
        return NULL;
    }

    printf("[Web Server] Listening on port 80...\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd >= 0) {
            printf("[Web Server] Client connected from %s:%d\n",
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

            char buffer[256];
            int read_bytes = read(client_fd, buffer, sizeof(buffer) - 1);
            if (read_bytes > 0) {
                buffer[read_bytes] = '\0';
                const char *response =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/html\r\n"
                    "Connection: close\r\n"
                    "\r\n"
                    "<!DOCTYPE html>\n"
                    "<html>\n"
                    "<head><title>STM32 Simulated Linux</title></head>\n"
                    "<body>\n"
                    "<h1>Hello from STM32 Simulated Linux!</h1>\n"
                    "<p>This web page is served from a simulated POSIX socket layer"
                    " running on FreeRTOS inside QEMU.</p>\n"
                    "</body>\n"
                    "</html>\n";
                write(client_fd, response, strlen(response));
            }
            close(client_fd);
        } else {
            usleep(100000);
        }
    }

    close(server_fd);
    return NULL;
}

void* main_posix_app(void* arg) {
    (void)arg;

    if (pthread_mutex_init(&g_counter_mutex, NULL) != 0) {
        printf("Failed to initialize mutex\n");
        return NULL;
    }

    if (sem_init(&g_job_semaphore, 0, 0) != 0) {
        printf("Failed to initialize semaphore\n");
        pthread_mutex_destroy(&g_counter_mutex);
        return NULL;
    }

    pthread_t thread1, thread2, sem_thread, web_server;

    thread_config_t *config1 = pvPortMalloc(sizeof(thread_config_t));
    if (config1 != NULL) { config1->thread_id = 1; config1->iterations = 5; }

    thread_config_t *config2 = pvPortMalloc(sizeof(thread_config_t));
    if (config2 != NULL) { config2->thread_id = 2; config2->iterations = 5; }

    if (pthread_create(&thread1, NULL, worker_thread_mutex, config1) != 0)
        printf("Failed to create Worker 1\n");

    if (pthread_create(&thread2, NULL, worker_thread_mutex, config2) != 0)
        printf("Failed to create Worker 2\n");

    if (pthread_create(&sem_thread, NULL, worker_thread_semaphore, NULL) != 0)
        printf("Failed to create Semaphore Worker\n");

    if (pthread_create(&web_server, NULL, web_server_thread, NULL) != 0)
        printf("Failed to create Web Server thread\n");

    /* Let workers run, then signal the semaphore worker */
    sleep(2);
    printf("[Main] Posting to semaphore...\n");
    sem_post(&g_job_semaphore);

    void *status1 = NULL, *status2 = NULL, *status_sem = NULL;

    printf("[Main] Joining Worker 1...\n");
    pthread_join(thread1, &status1);
    printf("[Main] Worker 1 joined with status: %d\n", (int)(uintptr_t)status1);

    printf("[Main] Joining Worker 2...\n");
    pthread_join(thread2, &status2);
    printf("[Main] Worker 2 joined with status: %d\n", (int)(uintptr_t)status2);

    printf("[Main] Joining Semaphore Worker...\n");
    pthread_join(sem_thread, &status_sem);
    printf("[Main] Semaphore Worker joined.\n");

    printf("[Main] Joining Web Server...\n");
    pthread_join(web_server, NULL);
    printf("[Main] Web Server joined.\n");

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

    vTaskStartScheduler();
}