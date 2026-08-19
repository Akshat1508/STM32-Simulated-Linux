/*
 * ============================================================================
 * File: main_blinky.c
 * Description: POSIX Concurrency Verification Suite & Embedded Web Server Demo
 * Target Platform: ARM Cortex-M3 (QEMU MPS2 AN385 / STM32)
 *
 * This file implements the main application demonstration for the simulated
 * Linux environment on FreeRTOS. It exercises and validates:
 *   1. POSIX Thread Creation & Joining (pthread_create, pthread_join)
 *   2. Mutual Exclusion Locks (pthread_mutex_*) protecting shared variables
 *   3. Counting Semaphore Synchronization (sem_init, sem_wait, sem_post)
 *   4. Timing Conversions (sleep, usleep)
 *   5. Multithreaded BSD Socket Networking with LwIP (HTTP Web Server)
 * ============================================================================
 */

/* Standard C library includes */
#include <stdio.h>
#include <string.h>

/* FreeRTOS Kernel API */
#include "FreeRTOS.h"
#include "task.h"

/* Decoupled POSIX Compatibility Layer Header */
#include "posix_shim.h"      /* pthread_*, sem_*, sleep, usleep */

/* LwIP TCP/IP Stack & BSD Socket Headers */
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"

/* External hardware Ethernet driver initialization routine */
err_t ethernetif_init(struct netif *netif);

/* ============================================================================
 * Global Shared Resources & Synchronization Primitives
 * ============================================================================
 */

/* Shared counter variable incremented concurrently by multiple worker threads */
static int g_shared_counter = 0;

/* POSIX Mutex protecting g_shared_counter from data races */
static pthread_mutex_t g_counter_mutex;

/* POSIX Counting Semaphore for IPC signaling between main thread and worker */
static sem_t g_job_semaphore;

/* Structure passed as argument to configure each worker thread instance */
typedef struct {
    int thread_id;   /* Unique identifier for logging output */
    int iterations;  /* Number of increments to perform */
} thread_config_t;

/* ============================================================================
 * Worker Thread 1 & 2: Mutex Concurrency Test Routine
 *
 * Demonstrates mutual exclusion locking across concurrent threads. Each worker
 * locks g_counter_mutex, reads the shared counter, introduces an artificial
 * 50 ms delay to widen the race condition window, increments the counter, and
 * unlocks the mutex.
 * ============================================================================
 */
void *worker_thread_mutex(void *arg)
{
    thread_config_t *config = (thread_config_t *)arg;
    printf("[Worker %d] Started. Incrementing counter %d times...\n",
           config->thread_id, config->iterations);

    for (int i = 0; i < config->iterations; i++) {
        /* Acquire mutual exclusion lock (blocks if held by another thread) */
        pthread_mutex_lock(&g_counter_mutex);

        /* Critical Section: Read shared resource */
        int temp = g_shared_counter;

        /*
         * Artificial 50 ms sleep: Yields CPU to test scheduler preemption.
         * If mutex locking failed, another worker would corrupt 'temp'.
         */
        usleep(50000);

        /* Write updated value back to shared counter */
        g_shared_counter = temp + 1;

        /* Release mutual exclusion lock */
        pthread_mutex_unlock(&g_counter_mutex);

        /* Brief 10 ms pause between loop iterations to allow thread interleaving */
        usleep(10000);
    }

    printf("[Worker %d] Finished.\n", config->thread_id);

    int id = config->thread_id;
    vPortFree(config); /* Reclaim parameter memory from FreeRTOS heap */

    /* Return worker thread ID as exit status pointer */
    return (void *)(uintptr_t)id;
}

/* ============================================================================
 * Worker Thread: Counting Semaphore IPC Test Routine
 *
 * Demonstrates thread suspension and inter-task signaling using sem_wait().
 * Blocks immediately on g_job_semaphore until main_posix_app issues sem_post().
 * ============================================================================
 */
void *worker_thread_semaphore(void *arg)
{
    (void)arg;
    printf("[Sem Worker] Waiting for semaphore...\n");

    /* Block indefinitely until semaphore count is incremented by main thread */
    sem_wait(&g_job_semaphore);

    printf("[Sem Worker] Semaphore received! Running task...\n");

    /* Simulate workload execution */
    sleep(1);

    printf("[Sem Worker] Task completed. Exiting.\n");
    return NULL;
}

/* ============================================================================
 * Web Server Thread: BSD Socket HTTP Daemon
 *
 * Initializes the LwIP TCP/IP stack in OS mode, assigns a static IP address,
 * registers the LAN9118 Ethernet network interface, creates a TCP listening
 * socket on port 80, accepts client connections, and serves HTTP 200 responses.
 * ============================================================================
 */
void *web_server_thread(void *arg)
{
    (void)arg;

    /*
     * 1. Initialize LwIP Core Stack:
     * Spawns the internal tcpip_thread message dispatcher task.
     */
    tcpip_init(NULL, NULL);

    /*
     * 2. Configure IPv4 Network Parameters for QEMU User-Mode Network:
     * - Simulated Device IP: 10.0.2.15
     * - Subnet Mask: 255.255.255.0
     * - Default Gateway: 10.0.2.2 (QEMU NAT host gateway)
     */
    ip_addr_t ipaddr, netmask, gw;
    IP4_ADDR(&ipaddr, 10, 0, 2, 15);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 10, 0, 2, 2);

    /*
     * 3. Register SMSC9118 Network Interface:
     * Binds ethernetif_init driver and tcpip_input packet receiver callback.
     */
    static struct netif main_netif;
    netif_add(&main_netif, &ipaddr, &netmask, &gw, NULL, ethernetif_init, tcpip_input);
    netif_set_default(&main_netif);
    netif_set_up(&main_netif);

    printf("[Web Server] LwIP Initialized. IP address: 10.0.2.15\n");

    /*
     * 4. Create BSD TCP Streaming Socket:
     * Uses standard socket() API mapped to LwIP sequential socket layer.
     */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        printf("[Web Server] Failed to create socket\n");
        return NULL;
    }

    /* Configure server socket address structure */
    struct sockaddr_in address;
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(80); /* Virtual Port 80 (forwarded from host 8080) */

    /* Bind socket descriptor to port 80 */
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        printf("[Web Server] Bind failed\n");
        close(server_fd);
        return NULL;
    }

    /* Place socket into passive listening mode with connection backlog of 5 */
    if (listen(server_fd, 5) < 0) {
        printf("[Web Server] Listen failed\n");
        close(server_fd);
        return NULL;
    }

    printf("[Web Server] Listening on port 80...\n");

    /*
     * 5. Main Connection Dispatch Loop:
     * Accepts incoming TCP connections from host (via curl http://localhost:8080).
     */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        /* Block until a client TCP handshake completes */
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd >= 0) {
            printf("[Web Server] Client connected from %s:%d\n",
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

            char buffer[256];
            /* Read incoming HTTP request string */
            int read_bytes = read(client_fd, buffer, sizeof(buffer) - 1);
            if (read_bytes > 0) {
                buffer[read_bytes] = '\0';

                /* Formulate HTTP/1.1 200 OK HTML payload */
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

                /* Transmit HTML payload over network interface */
                write(client_fd, response, strlen(response));
            }
            /* Graceful TCP close (sends FIN packet) */
            close(client_fd);
        } else {
            /* If non-blocking/error, yield execution briefly */
            usleep(100000);
        }
    }

    close(server_fd);
    return NULL;
}

/* ============================================================================
 * Master Application Thread: main_posix_app
 *
 * Orchestrates the simulated Linux user-space environment:
 *   1. Initializes mutexes and counting semaphores
 *   2. Spawns worker threads and HTTP server via pthread_create()
 *   3. Triggers semaphore signaling via sem_post()
 *   4. Blocks on thread completion via pthread_join()
 *   5. Validates thread synchronization integrity and shared counter value
 * ============================================================================
 */
void *main_posix_app(void *arg)
{
    (void)arg;

    /* Initialize mutual exclusion lock */
    if (pthread_mutex_init(&g_counter_mutex, NULL) != 0) {
        printf("Failed to initialize mutex\n");
        return NULL;
    }

    /* Initialize counting semaphore with initial count 0 */
    if (sem_init(&g_job_semaphore, 0, 0) != 0) {
        printf("Failed to initialize semaphore\n");
        pthread_mutex_destroy(&g_counter_mutex);
        return NULL;
    }

    pthread_t thread1, thread2, sem_thread, web_server;

    /* Allocate thread configuration blocks from heap */
    thread_config_t *config1 = pvPortMalloc(sizeof(thread_config_t));
    if (config1 != NULL) { config1->thread_id = 1; config1->iterations = 5; }

    thread_config_t *config2 = pvPortMalloc(sizeof(thread_config_t));
    if (config2 != NULL) { config2->thread_id = 2; config2->iterations = 5; }

    /* Spawn Worker Thread 1 (5 increments) */
    if (pthread_create(&thread1, NULL, worker_thread_mutex, config1) != 0)
        printf("Failed to create Worker 1\n");

    /* Spawn Worker Thread 2 (5 increments) */
    if (pthread_create(&thread2, NULL, worker_thread_mutex, config2) != 0)
        printf("Failed to create Worker 2\n");

    /* Spawn Semaphore Worker Thread */
    if (pthread_create(&sem_thread, NULL, worker_thread_semaphore, NULL) != 0)
        printf("Failed to create Semaphore Worker\n");

    /* Spawn POSIX BSD Socket HTTP Web Server Daemon */
    if (pthread_create(&web_server, NULL, web_server_thread, NULL) != 0)
        printf("Failed to create Web Server thread\n");

    /*
     * Allow worker threads to execute, then signal the semaphore worker
     * to test inter-thread IPC synchronization.
     */
    sleep(2);
    printf("[Main] Posting to semaphore...\n");
    sem_post(&g_job_semaphore);

    void *status1 = NULL, *status2 = NULL, *status_sem = NULL;

    /* Wait for Worker 1 completion and verify exit status */
    printf("[Main] Joining Worker 1...\n");
    pthread_join(thread1, &status1);
    printf("[Main] Worker 1 joined with status: %d\n", (int)(uintptr_t)status1);

    /* Wait for Worker 2 completion and verify exit status */
    printf("[Main] Joining Worker 2...\n");
    pthread_join(thread2, &status2);
    printf("[Main] Worker 2 joined with status: %d\n", (int)(uintptr_t)status2);

    /* Wait for Semaphore Worker completion */
    printf("[Main] Joining Semaphore Worker...\n");
    pthread_join(sem_thread, &status_sem);
    printf("[Main] Semaphore Worker joined.\n");

    /* Wait for Web Server Daemon (runs continuously) */
    printf("[Main] Joining Web Server...\n");
    pthread_join(web_server, NULL);
    printf("[Main] Web Server joined.\n");

    /*
     * Concurrency Verification:
     * Worker 1 (5) + Worker 2 (5) must equal exactly 10 if mutex locking is sound.
     */
    printf("[Main] Final shared counter value: %d (Expected: 10)\n", g_shared_counter);

    /* Clean up synchronization resources */
    pthread_mutex_destroy(&g_counter_mutex);
    sem_destroy(&g_job_semaphore);

    printf("--- Simulated Linux Environment Terminated Successfully ---\n");
    return NULL;
}

/* ============================================================================
 * Main Blinky Entry Point: main_blinky
 *
 * Invoked by main() post-reset. Creates the master POSIX application thread,
 * detaches it, and starts the FreeRTOS real-time kernel scheduler.
 * ============================================================================
 */
void main_blinky(void)
{
    printf("--- Booting Simulated Linux Environment ---\n");

    pthread_t main_thread;

    /* Spawn master POSIX orchestrator thread */
    if (pthread_create(&main_thread, NULL, main_posix_app, NULL) == 0) {
        /* Detach thread so it manages its own lifecycle */
        pthread_detach(main_thread);
    } else {
        printf("Failed to create main POSIX app thread\n");
    }

    /*
     * Start the FreeRTOS Scheduler:
     * - Configures ARM Cortex-M SysTick timer for 1 ms tick interrupts.
     * - Sets PendSV interrupt priority to lowest level.
     * - Switches CPU stack pointer from MSP to PSP for task execution.
     * - Executes 'svc 0' to jump into the first ready task.
     */
    vTaskStartScheduler();
}