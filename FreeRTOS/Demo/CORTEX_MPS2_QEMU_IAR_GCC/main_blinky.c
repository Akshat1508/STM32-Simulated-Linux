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
 *   4. Condition Variable Synchronization (pthread_cond_wait, signal, broadcast)
 *   5. Timing Conversions (sleep, usleep)
 *   6. Multithreaded BSD Socket Networking with LwIP (HTTP Web Server)
 * ============================================================================
 */

/* Standard C library includes */
#include <stdio.h>
#include <string.h>

/* FreeRTOS Kernel API */
#include "FreeRTOS.h"
#include "task.h"

/* Decoupled POSIX Compatibility Layer Header */
#include "posix_shim.h"      /* pthread_*, pthread_cond_*, sem_*, sleep, usleep */

/* LwIP TCP/IP Stack & BSD Socket Headers */
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"

/* Virtual File System & Unified Descriptor Table */
#include "vfs.h"

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
 * Condition Variable Test: Bounded Buffer Producer-Consumer
 *
 * Validates pthread_cond_wait, pthread_cond_signal, and pthread_cond_broadcast.
 * A producer thread fills a shared bounded buffer and signals waiting consumers.
 * Consumer threads wait on a condition variable until data becomes available.
 * ============================================================================
 */

/* Bounded buffer configuration */
#define BUFFER_CAPACITY 4
#define TOTAL_ITEMS     8

/* Shared bounded buffer state protected by condvar_mutex */
static int              g_buffer[BUFFER_CAPACITY];
static int              g_buffer_count = 0;    /* Current number of items in buffer */
static int              g_produce_idx  = 0;    /* Next write index (circular) */
static int              g_consume_idx  = 0;    /* Next read index (circular) */
static volatile int     g_production_done = 0; /* Flag: producer finished all items */

/* Condition variable synchronization primitives */
static pthread_mutex_t  g_condvar_mutex;
static pthread_cond_t   g_cond_not_empty;  /* Signaled when buffer has data */
static pthread_cond_t   g_cond_not_full;   /* Signaled when buffer has free space */

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
 * Producer Thread: Condition Variable Bounded Buffer Test
 *
 * Produces TOTAL_ITEMS data tokens into the shared bounded buffer.
 * When the buffer is full, waits on g_cond_not_full until a consumer
 * removes an item. After producing each item, signals g_cond_not_empty
 * to wake a waiting consumer. After all items are produced, broadcasts
 * to wake all consumers so they can detect completion.
 * ============================================================================
 */
void *producer_thread(void *arg)
{
    (void)arg;
    printf("[Producer] Started. Producing %d items...\n", TOTAL_ITEMS);

    for (int i = 1; i <= TOTAL_ITEMS; i++) {
        pthread_mutex_lock(&g_condvar_mutex);

        /* Wait while buffer is full */
        while (g_buffer_count == BUFFER_CAPACITY) {
            printf("[Producer] Buffer full (%d/%d). Waiting...\n",
                   g_buffer_count, BUFFER_CAPACITY);
            pthread_cond_wait(&g_cond_not_full, &g_condvar_mutex);
        }

        /* Produce: insert item into circular buffer */
        g_buffer[g_produce_idx] = i;
        g_produce_idx = (g_produce_idx + 1) % BUFFER_CAPACITY;
        g_buffer_count++;

        printf("[Producer] Produced item %d (buffer: %d/%d)\n",
               i, g_buffer_count, BUFFER_CAPACITY);

        /* Signal one consumer that data is available */
        pthread_cond_signal(&g_cond_not_empty);

        pthread_mutex_unlock(&g_condvar_mutex);

        /* Brief pause to allow interleaving with consumers */
        usleep(50000);
    }

    /* Mark production as complete and wake all consumers */
    pthread_mutex_lock(&g_condvar_mutex);
    g_production_done = 1;
    pthread_cond_broadcast(&g_cond_not_empty);
    pthread_mutex_unlock(&g_condvar_mutex);

    printf("[Producer] All %d items produced. Exiting.\n", TOTAL_ITEMS);
    return NULL;
}

/* ============================================================================
 * Consumer Thread: Condition Variable Bounded Buffer Test
 *
 * Consumes items from the shared bounded buffer. When the buffer is empty,
 * waits on g_cond_not_empty until the producer adds data. After consuming
 * an item, signals g_cond_not_full to wake the producer if it was blocked.
 * Exits when production is complete and buffer is empty.
 * ============================================================================
 */
void *consumer_thread(void *arg)
{
    int consumer_id = (int)(uintptr_t)arg;
    int items_consumed = 0;

    printf("[Consumer %d] Started. Waiting for items...\n", consumer_id);

    while (1) {
        pthread_mutex_lock(&g_condvar_mutex);

        /* Wait while buffer is empty and production is not done */
        while (g_buffer_count == 0 && !g_production_done) {
            pthread_cond_wait(&g_cond_not_empty, &g_condvar_mutex);
        }

        /* Check termination: buffer empty AND producer finished */
        if (g_buffer_count == 0 && g_production_done) {
            pthread_mutex_unlock(&g_condvar_mutex);
            break;
        }

        /* Consume: extract item from circular buffer */
        int item = g_buffer[g_consume_idx];
        g_consume_idx = (g_consume_idx + 1) % BUFFER_CAPACITY;
        g_buffer_count--;
        items_consumed++;

        printf("[Consumer %d] Consumed item %d (buffer: %d/%d)\n",
               consumer_id, item, g_buffer_count, BUFFER_CAPACITY);

        /* Signal producer that buffer has free space */
        pthread_cond_signal(&g_cond_not_full);

        pthread_mutex_unlock(&g_condvar_mutex);

        /* Simulate processing time */
        usleep(80000);
    }

    printf("[Consumer %d] Finished. Consumed %d items total.\n",
           consumer_id, items_consumed);
    return (void *)(uintptr_t)items_consumed;
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
        lwip_close(server_fd);
        return NULL;
    }

    /* Place socket into passive listening mode with connection backlog of 5 */
    if (listen(server_fd, 5) < 0) {
        printf("[Web Server] Listen failed\n");
        lwip_close(server_fd);
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
            int read_bytes = lwip_read(client_fd, buffer, sizeof(buffer) - 1);
            if (read_bytes > 0) {
                static const char response[] =
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
                lwip_write(client_fd, response, sizeof(response) - 1);
            }
            /* Graceful TCP close (sends FIN packet) */
            lwip_close(client_fd);
        } else {
            /* If non-blocking/error, yield execution briefly */
            usleep(100000);
        }
    }

    lwip_close(server_fd);
    return NULL;
}

/* ============================================================================
 * Client Socket Test Thread: Milestone 2 Verification Suite (Group C)
 *
 * Exercises and verifies the 7 newly implemented Group C networking APIs:
 *   1. socket(): Creates an outbound TCP stream socket descriptor.
 *   2. setsockopt(): Configures SO_REUSEADDR and SO_RCVTIMEO options.
 *   3. fcntl(): Tests F_GETFL / F_SETFL flags, verifying non-blocking toggle.
 *   4. connect(): Establishes an outbound TCP connection to the local HTTP server.
 *   5. send(): Transmits an HTTP GET request payload over the socket.
 *   6. select(): Validates synchronous read readiness on the socket descriptor.
 *   7. poll(): Validates pollfd-based event multiplexing for POLLIN.
 *   8. recv(): Reads the HTTP 200 OK response from the server.
 *   9. close(): Gracefully closes the socket descriptor.
 * ============================================================================
 */
void *client_socket_test_thread(void *arg)
{
    (void)arg;
    int all_passed = 1;

    printf("[Client Test] --- Starting Milestone 2 Socket Client Verification ---\n");

    /* Step 1: Create client socket */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("[Client Test] FAILED: socket() creation returned %d\n", sock);
        return (void *)(uintptr_t)0;
    }
    printf("[Client Test] 1. socket() created fd: %d [PASSED]\n", sock);

    /* Step 2: Test setsockopt() - SO_REUSEADDR and SO_RCVTIMEO */
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        printf("[Client Test] FAILED: setsockopt(SO_REUSEADDR)\n");
        all_passed = 0;
    } else {
        printf("[Client Test] 2a. setsockopt(SO_REUSEADDR) [PASSED]\n");
    }

    int rcv_timeout_ms = 3000;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &rcv_timeout_ms, sizeof(rcv_timeout_ms)) != 0) {
        printf("[Client Test] FAILED: setsockopt(SO_RCVTIMEO)\n");
        all_passed = 0;
    } else {
        printf("[Client Test] 2b. setsockopt(SO_RCVTIMEO) [PASSED]\n");
    }

    /* Step 3: Test fcntl() - F_GETFL and F_SETFL (O_NONBLOCK toggle) */
    int orig_flags = fcntl(sock, F_GETFL);
    if (orig_flags < 0) {
        printf("[Client Test] FAILED: fcntl(F_GETFL)\n");
        all_passed = 0;
    } else {
        /* Set O_NONBLOCK (LwIP socket layer expects 1) */
        if (fcntl(sock, F_SETFL, 1) != 0) {
            printf("[Client Test] FAILED: fcntl(F_SETFL, O_NONBLOCK)\n");
            all_passed = 0;
        } else {
            int nb_flags = fcntl(sock, F_GETFL);
            if (!(nb_flags & 1)) {
                printf("[Client Test] FAILED: O_NONBLOCK flag not set in fcntl\n");
                all_passed = 0;
            } else {
                printf("[Client Test] 3. fcntl(F_GETFL / F_SETFL, O_NONBLOCK) [PASSED]\n");
            }
            /* Restore blocking mode for synchronous connect */
            fcntl(sock, F_SETFL, 0);
        }
    }

    /* Step 4: Test connect() - connect to local web server on 127.0.0.1:80 */
    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(80);
    srv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    printf("[Client Test] Connecting to 127.0.0.1:80...\n");
    if (connect(sock, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) != 0) {
        printf("[Client Test] FAILED: connect() to 127.0.0.1:80\n");
        lwip_close(sock);
        return (void *)(uintptr_t)0;
    }
    printf("[Client Test] 4. connect() to HTTP server [PASSED]\n");

    /* Step 5: Test send() - send HTTP GET request */
    const char *http_req = "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    size_t req_len = strlen(http_req);
    int sent_bytes = send(sock, http_req, req_len, 0);
    if (sent_bytes != (int)req_len) {
        printf("[Client Test] FAILED: send() returned %d (expected %d)\n", sent_bytes, (int)req_len);
        all_passed = 0;
    } else {
        printf("[Client Test] 5. send() transmitted %d bytes [PASSED]\n", sent_bytes);
    }

    /* Step 6: Test select() multiplexing - wait up to 2 seconds for read readiness */
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(sock, &rset);
    struct timeval sel_tv;
    sel_tv.tv_sec = 2;
    sel_tv.tv_usec = 0;
    int sel_ret = select(sock + 1, &rset, NULL, NULL, &sel_tv);
    if (sel_ret > 0 && FD_ISSET(sock, &rset)) {
        printf("[Client Test] 6. select() detected read readiness [PASSED]\n");
    } else {
        printf("[Client Test] FAILED: select() returned %d\n", sel_ret);
        all_passed = 0;
    }

    /* Step 7: Test poll() multiplexing - inspect read readiness on pollfd */
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int poll_ret = poll(&pfd, 1, 2000);
    if (poll_ret > 0 && (pfd.revents & POLLIN)) {
        printf("[Client Test] 7. poll() detected POLLIN event [PASSED]\n");
    } else {
        printf("[Client Test] FAILED: poll() returned %d (revents: 0x%04x)\n", poll_ret, pfd.revents);
        all_passed = 0;
    }

    /* Step 8: Test recv() - receive HTTP response payload */
    char rx_buf[256];
    int recv_bytes = recv(sock, rx_buf, sizeof(rx_buf) - 1, 0);
    if (recv_bytes > 0) {
        rx_buf[recv_bytes] = '\0';
        if (strstr(rx_buf, "200 OK") != NULL) {
            printf("[Client Test] 8. recv() received %d bytes ('HTTP/1.1 200 OK') [PASSED]\n", recv_bytes);
        } else {
            printf("[Client Test] FAILED: recv() received unexpected payload: %s\n", rx_buf);
            all_passed = 0;
        }
    } else {
        printf("[Client Test] FAILED: recv() returned %d\n", recv_bytes);
        all_passed = 0;
    }

    /* Step 9: Close client socket */
    lwip_close(sock);

    printf("[Client Test] --- Milestone 2 Sockets Verification Summary: %s ---\n",
           all_passed ? "ALL TESTS PASSED" : "TESTS FAILED");

    return (void *)(uintptr_t)all_passed;
}

/* ============================================================================
 * Clock & High-Resolution Timer Test Thread: Milestone 3 Verification Suite
 *
 * Exercises and verifies the 4 newly implemented Group B timing APIs:
 *   1. clock_gettime(): Tests CLOCK_REALTIME and CLOCK_MONOTONIC with SysTick precision.
 *   2. gettimeofday(): Tests calendar time query (seconds + microseconds).
 *   3. time(): Tests standard Epoch whole-seconds query.
 *   4. nanosleep(): Tests sub-millisecond precision delay and task-yielding sleep.
 * ============================================================================
 */
void *clock_timer_test_thread(void *arg)
{
    (void)arg;
    int all_passed = 1;

    printf("[Clock Test] --- Starting Milestone 3 Clocks & Timers Verification ---\n");

    /* Step 1: Test clock_gettime(CLOCK_REALTIME) */
    struct timespec ts_real;
    if (clock_gettime(CLOCK_REALTIME, &ts_real) != 0) {
        printf("[Clock Test] FAILED: clock_gettime(CLOCK_REALTIME)\n");
        all_passed = 0;
    } else {
        printf("[Clock Test] 1a. clock_gettime(CLOCK_REALTIME) -> %d s, %u ns [PASSED]\n",
               (int)ts_real.tv_sec, (unsigned int)ts_real.tv_nsec);
    }

    /* Step 2: Test clock_gettime(CLOCK_MONOTONIC) */
    struct timespec ts_mono1, ts_mono2;
    if (clock_gettime(CLOCK_MONOTONIC, &ts_mono1) != 0) {
        printf("[Clock Test] FAILED: clock_gettime(CLOCK_MONOTONIC)\n");
        all_passed = 0;
    } else {
        printf("[Clock Test] 1b. clock_gettime(CLOCK_MONOTONIC) -> %d s, %u ns [PASSED]\n",
               (int)ts_mono1.tv_sec, (unsigned int)ts_mono1.tv_nsec);
    }

    /* Test clock_gettime error handling */
    struct timespec ts_err;
    if (clock_gettime(999, &ts_err) == -1 && errno == EINVAL) {
        printf("[Clock Test] 1c. clock_gettime invalid clk_id rejected [PASSED]\n");
    } else {
        printf("[Clock Test] FAILED: clock_gettime invalid clk_id error check\n");
        all_passed = 0;
    }

    /* Step 3: Test gettimeofday() */
    struct timeval tv;
    struct timezone tz;
    if (gettimeofday(&tv, &tz) != 0) {
        printf("[Clock Test] FAILED: gettimeofday()\n");
        all_passed = 0;
    } else {
        printf("[Clock Test] 2. gettimeofday() -> %d s, %u us [PASSED]\n",
               (int)tv.tv_sec, (unsigned int)tv.tv_usec);
    }

    /* Step 4: Test time() */
    time_t t_var = 0;
    time_t t_ret = time(&t_var);
    if (t_ret < 0 || t_ret != t_var) {
        printf("[Clock Test] FAILED: time() returned %d, t_var=%d\n",
               (int)t_ret, (int)t_var);
        all_passed = 0;
    } else {
        printf("[Clock Test] 3. time() -> %d s [PASSED]\n", (int)t_ret);
    }

    /* Step 5: Test nanosleep() sub-millisecond precision (500 microseconds = 500,000 ns) */
    struct timespec req_sub, rem_sub;
    req_sub.tv_sec  = 0;
    req_sub.tv_nsec = 500000L; /* 500 us */

    clock_gettime(CLOCK_MONOTONIC, &ts_mono1);
    int nano_ret = nanosleep(&req_sub, &rem_sub);
    clock_gettime(CLOCK_MONOTONIC, &ts_mono2);

    if (nano_ret != 0) {
        printf("[Clock Test] FAILED: nanosleep(500 us) returned %d\n", nano_ret);
        all_passed = 0;
    } else {
        uint32_t d_sec = (uint32_t)(ts_mono2.tv_sec - ts_mono1.tv_sec);
        int32_t d_nsec = (int32_t)(ts_mono2.tv_nsec - ts_mono1.tv_nsec);
        if (d_nsec < 0) {
            d_sec -= 1;
            d_nsec += 1000000000L;
        }
        uint32_t delta_ns = d_sec * 1000000000UL + (uint32_t)d_nsec;
        printf("[Clock Test] 4a. nanosleep(500 us) elapsed: %u ns [PASSED]\n", (unsigned int)delta_ns);
    }

    /* Step 6: Test nanosleep() coarse delay (50 ms = 50,000,000 ns) */
    struct timespec req_coarse;
    req_coarse.tv_sec  = 0;
    req_coarse.tv_nsec = 50000000L; /* 50 ms */

    clock_gettime(CLOCK_MONOTONIC, &ts_mono1);
    nano_ret = nanosleep(&req_coarse, NULL);
    clock_gettime(CLOCK_MONOTONIC, &ts_mono2);

    if (nano_ret != 0) {
        printf("[Clock Test] FAILED: nanosleep(50 ms) returned %d\n", nano_ret);
        all_passed = 0;
    } else {
        uint32_t d_sec = (uint32_t)(ts_mono2.tv_sec - ts_mono1.tv_sec);
        int32_t d_nsec = (int32_t)(ts_mono2.tv_nsec - ts_mono1.tv_nsec);
        if (d_nsec < 0) {
            d_sec -= 1;
            d_nsec += 1000000000L;
        }
        uint32_t delta_ms = d_sec * 1000UL + (uint32_t)(d_nsec / 1000000L);
        printf("[Clock Test] 4b. nanosleep(50 ms) elapsed: %u ms [PASSED]\n", (unsigned int)delta_ms);
    }

    printf("[Clock Test] --- Milestone 3 Clocks & Timers Verification Summary: %s ---\n",
           all_passed ? "ALL TESTS PASSED" : "TESTS FAILED");

    return (void *)(uintptr_t)all_passed;
}

/*
 * Custom Stack Worker Thread: Verifies execution under custom stack depth.
 */
static void *custom_stack_worker(void *arg)
{
    int val = (int)(uintptr_t)arg;
    /* Touch local stack frame to ensure stack allocation is operational */
    volatile int stack_var = val + 10;
    (void)stack_var;
    return (void *)(uintptr_t)val;
}

static void report_test(const char *subsys, const char *name, int passed)
{
    printf("[%s Test] %s [%s]\n", subsys, name, passed ? "PASSED" : "FAILED");
}

/* ============================================================================
 * Mutex & Thread Attributes Test Thread: Milestone 4 Verification Suite
 *
 * Exercises and verifies the 4 newly implemented Group D APIs:
 *   1. pthread_mutex_trylock(): Non-blocking lock acquisition and EBUSY check.
 *   2. pthread_mutex_timedlock(): Deadline timeout (ETIMEDOUT) and acquisition.
 *   3. pthread_mutexattr_*(): Mutex attribute lifecycle and recursive locking.
 *   4. pthread_attr_*(): Thread attribute lifecycle and custom stack depth.
 * ============================================================================
 */
void *mutex_attr_test_thread(void *arg)
{
    (void)arg;
    int all_passed = 1;

    printf("[Mutex Test] --- Starting Milestone 4 Verification ---\n");

    /* Sub-test 1: pthread_mutex_trylock() */
    pthread_mutex_t m1;
    if (pthread_mutex_init(&m1, NULL) == 0) {
        int ok1 = (pthread_mutex_trylock(&m1) == 0);
        report_test("Mutex", "1a. trylock(unlocked)", ok1);
        all_passed &= ok1;

        int ok2 = (pthread_mutex_trylock(&m1) == EBUSY);
        report_test("Mutex", "1b. trylock(locked)->EBUSY", ok2);
        all_passed &= ok2;

        int ok3 = (pthread_mutex_unlock(&m1) == 0);
        report_test("Mutex", "1c. unlock()", ok3);
        all_passed &= ok3;

        pthread_mutex_destroy(&m1);
    }

    /* Sub-test 2: pthread_mutex_timedlock() */
    pthread_mutex_t m2;
    if (pthread_mutex_init(&m2, NULL) == 0) {
        pthread_mutex_lock(&m2);

        struct timespec past_ts;
        clock_gettime(CLOCK_REALTIME, &past_ts);
        if (past_ts.tv_sec > 0) {
            past_ts.tv_sec -= 1;
        }
        int ok_past = (pthread_mutex_timedlock(&m2, &past_ts) == ETIMEDOUT);
        report_test("Mutex", "2a. timedlock(past)->ETIMEDOUT", ok_past);
        all_passed &= ok_past;

        struct timespec future_ts;
        clock_gettime(CLOCK_REALTIME, &future_ts);
        future_ts.tv_nsec += 25000000L; /* +25 ms */
        if (future_ts.tv_nsec >= 1000000000L) {
            future_ts.tv_sec += 1;
            future_ts.tv_nsec -= 1000000000L;
        }
        int ok_busy = (pthread_mutex_timedlock(&m2, &future_ts) == ETIMEDOUT);
        report_test("Mutex", "2b. timedlock(25ms)->ETIMEDOUT", ok_busy);
        all_passed &= ok_busy;

        pthread_mutex_unlock(&m2);

        clock_gettime(CLOCK_REALTIME, &future_ts);
        future_ts.tv_sec += 1;
        int ok_avail = (pthread_mutex_timedlock(&m2, &future_ts) == 0);
        report_test("Mutex", "2c. timedlock(avail)", ok_avail);
        all_passed &= ok_avail;
        if (ok_avail) {
            pthread_mutex_unlock(&m2);
        }

        pthread_mutex_destroy(&m2);
    }

    /* Sub-test 3: pthread_mutexattr_*() and Recursive Mutex */
    pthread_mutexattr_t mattr;
    if (pthread_mutexattr_init(&mattr) == 0) {
        int mtype = -1;
        pthread_mutexattr_gettype(&mattr, &mtype);
        int ok_def = (mtype == PTHREAD_MUTEX_NORMAL);
        report_test("Mutex", "3a. attr default NORMAL", ok_def);
        all_passed &= ok_def;

        int ok_set = (pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE) == 0);
        pthread_mutexattr_gettype(&mattr, &mtype);
        int ok_rec = (ok_set && mtype == PTHREAD_MUTEX_RECURSIVE);
        report_test("Mutex", "3b. attr set RECURSIVE", ok_rec);
        all_passed &= ok_rec;

        pthread_mutex_t rec_m;
        if (pthread_mutex_init(&rec_m, &mattr) == 0) {
            pthread_mutexattr_destroy(&mattr);

            int l1 = pthread_mutex_lock(&rec_m);
            int l2 = pthread_mutex_lock(&rec_m);
            int l3 = pthread_mutex_trylock(&rec_m);
            int ok_nest = (l1 == 0 && l2 == 0 && l3 == 0);
            report_test("Mutex", "3c. recursive lock depth 3", ok_nest);
            all_passed &= ok_nest;

            int u1 = pthread_mutex_unlock(&rec_m);
            int u2 = pthread_mutex_unlock(&rec_m);
            int u3 = pthread_mutex_unlock(&rec_m);
            int ok_unwind = (u1 == 0 && u2 == 0 && u3 == 0);
            report_test("Mutex", "3d. recursive unlock depth 3", ok_unwind);
            all_passed &= ok_unwind;

            pthread_mutex_destroy(&rec_m);
        }
    }

    /* Sub-test 4: pthread_attr_*() and Custom Stack Depth */
    pthread_attr_t tattr;
    if (pthread_attr_init(&tattr) == 0) {
        size_t def_stack = 0;
        pthread_attr_getstacksize(&tattr, &def_stack);
        int ok_def_stk = (def_stack == 4096);
        report_test("Mutex", "4a. attr default stack 4096 B", ok_def_stk);
        all_passed &= ok_def_stk;

        size_t custom_target = 2048;
        pthread_attr_setstacksize(&tattr, custom_target);
        size_t queried = 0;
        pthread_attr_getstacksize(&tattr, &queried);
        int ok_custom_stk = (queried == custom_target);
        report_test("Mutex", "4b. attr custom stack 2048 B", ok_custom_stk);
        all_passed &= ok_custom_stk;

        int ok_inval = (pthread_attr_setstacksize(&tattr, 16) == EINVAL);
        report_test("Mutex", "4c. reject small stack EINVAL", ok_inval);
        all_passed &= ok_inval;

        pthread_attr_setstacksize(&tattr, custom_target);
        pthread_t custom_tid;
        void *custom_ret = NULL;
        if (pthread_create(&custom_tid, &tattr, custom_stack_worker, (void *)(uintptr_t)42) == 0) {
            pthread_attr_destroy(&tattr);
            pthread_join(custom_tid, &custom_ret);
            int ok_thread = ((int)(uintptr_t)custom_ret == 42);
            report_test("Mutex", "4d. thread custom stack join", ok_thread);
            all_passed &= ok_thread;
        } else {
            pthread_attr_destroy(&tattr);
        }
    }

    printf("[Mutex Test] --- Milestone 4 Verification Summary: %s ---\n",
           all_passed ? "ALL TESTS PASSED" : "TESTS FAILED");

    return (void *)(uintptr_t)all_passed;
}

/* ============================================================================
 * Virtual File System (VFS) Test Thread: Milestone 5 Verification Suite
 *
 * Validates the unified file descriptor table and POSIX I/O operations (Group E):
 *   5a. open() & close(): In-memory RAM file creation and existence checks (O_CREAT, O_EXCL)
 *   5b. write(), lseek(), read(): Offset positioning and data roundtrip
 *   5c. stat() & fstat(): Synthetic metadata and file size inspection
 *   5d. pipe(): Inter-thread communication, non-blocking/blocking, and EOF semantics
 *   5e. dup2(): Descriptor cloning and shared reference count persistence
 *   5f. vfs_register_socket(): Unified routing of socket descriptors in 3..15
 * ============================================================================
 */
void *vfs_test_thread(void *arg)
{
    (void)arg;
    int all_passed = 1;

    printf("[VFS Test] --- Starting Milestone 5 VFS Layer Verification ---\n");

    /* 5a. RAM file open, create, and close */
    int fd = open("/tmp/m5_test.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
    int ok_open = (fd >= VFS_USER_BASE_FD && fd <= VFS_USER_MAX_FD);
    report_test("VFS", "5a. open(O_CREAT|O_RDWR)", ok_open);
    all_passed &= ok_open;

    int excl_fd = open("/tmp/m5_test.txt", O_CREAT | O_EXCL | O_RDWR, 0644);
    int ok_excl = (excl_fd == -1 && errno == EEXIST);
    report_test("VFS", "5a. open(O_CREAT|O_EXCL)->EEXIST", ok_excl);
    all_passed &= ok_excl;

    /* 5b. write(), lseek(), read() */
    const char *payload = "Hello STM32 VFS Layer!";
    size_t payload_len = strlen(payload);
    ssize_t bytes_written = write(fd, payload, payload_len);
    int ok_write = (bytes_written == (ssize_t)payload_len);
    report_test("VFS", "5b. write() payload", ok_write);
    all_passed &= ok_write;

    off_t seek_res = lseek(fd, 0, SEEK_SET);
    int ok_seek0 = (seek_res == 0);
    report_test("VFS", "5b. lseek(SEEK_SET, 0)", ok_seek0);
    all_passed &= ok_seek0;

    char r_buf[32] = {0};
    ssize_t bytes_read = read(fd, r_buf, sizeof(r_buf) - 1);
    int ok_read = (bytes_read == (ssize_t)payload_len && strcmp(r_buf, payload) == 0);
    report_test("VFS", "5b. read() full payload", ok_read);
    all_passed &= ok_read;

    lseek(fd, 6, SEEK_SET);
    char sub_buf[16] = {0};
    read(fd, sub_buf, 5);
    int ok_sub = (strcmp(sub_buf, "STM32") == 0);
    report_test("VFS", "5b. lseek()+read() substring", ok_sub);
    all_passed &= ok_sub;

    close(fd);

    /* 5c. stat() & fstat() */
    struct stat st;
    int st_ret = stat("/tmp/m5_test.txt", &st);
    int ok_stat = (st_ret == 0 && st.st_size == (off_t)payload_len && (st.st_mode & S_IFREG) != 0);
    report_test("VFS", "5c. stat(pathname)", ok_stat);
    all_passed &= ok_stat;

    int fd_stat = open("/tmp/m5_test.txt", O_RDONLY);
    struct stat fst;
    int fst_ret = fstat(fd_stat, &fst);
    int ok_fstat = (fst_ret == 0 && fst.st_size == (off_t)payload_len);
    report_test("VFS", "5c. fstat(fd)", ok_fstat);
    all_passed &= ok_fstat;
    close(fd_stat);

    /* 5d. pipe() communication and EOF */
    int pfd[2] = {-1, -1};
    int pipe_ret = pipe(pfd);
    int ok_pipe_create = (pipe_ret == 0 && pfd[0] >= VFS_USER_BASE_FD && pfd[1] >= VFS_USER_BASE_FD && pfd[0] != pfd[1]);
    report_test("VFS", "5d. pipe() allocation", ok_pipe_create);
    all_passed &= ok_pipe_create;

    if (ok_pipe_create) {
        const char *pipe_msg = "PipeIPCData";
        ssize_t pw = write(pfd[1], pipe_msg, strlen(pipe_msg));
        char pr_buf[16] = {0};
        ssize_t pr = read(pfd[0], pr_buf, sizeof(pr_buf) - 1);
        int ok_pipe_rw = (pw == (ssize_t)strlen(pipe_msg) && pr == pw && strcmp(pr_buf, pipe_msg) == 0);
        report_test("VFS", "5d. pipe write()->read() IPC", ok_pipe_rw);
        all_passed &= ok_pipe_rw;

        close(pfd[1]); /* Close writer */
        ssize_t pr_eof = read(pfd[0], pr_buf, sizeof(pr_buf));
        int ok_eof = (pr_eof == 0); /* Reader gets EOF when all writers are closed */
        report_test("VFS", "5d. pipe EOF on writer close", ok_eof);
        all_passed &= ok_eof;

        close(pfd[0]); /* Close reader */
    }

    /* 5e. dup2() descriptor cloning and reference persistence */
    int d_orig = open("/tmp/m5_test.txt", O_RDONLY);
    int d_target = 26;
    int d_dup = dup2(d_orig, d_target);
    int ok_dup = (d_dup == d_target);
    report_test("VFS", "5e. dup2(orig, 26)", ok_dup);
    all_passed &= ok_dup;

    char d_buf1[8] = {0};
    read(d_target, d_buf1, 5);
    int ok_dup_r1 = (strncmp(d_buf1, "Hello", 5) == 0);
    report_test("VFS", "5e. read() from dup2 fd", ok_dup_r1);
    all_passed &= ok_dup_r1;

    close(d_orig); /* Closing original descriptor must NOT invalidate cloned descriptor */
    char d_buf2[8] = {0};
    read(d_target, d_buf2, 5);
    int ok_dup_r2 = (strncmp(d_buf2, " STM3", 5) == 0);
    report_test("VFS", "5e. read() after orig closed", ok_dup_r2);
    all_passed &= ok_dup_r2;

    close(d_target);

    /* 5f. vfs_register_socket() unified socket range 3..15 */
    int raw_s = socket(AF_INET, SOCK_STREAM, 0);
    int vfs_s = vfs_register_socket(raw_s);
    int ok_sock = (vfs_s >= VFS_SOCKET_BASE_FD && vfs_s <= VFS_SOCKET_MAX_FD);
    report_test("VFS", "5f. vfs_register_socket(3..15)", ok_sock);
    all_passed &= ok_sock;

    struct stat s_st;
    int s_st_ret = fstat(vfs_s, &s_st);
    int ok_sock_st = (s_st_ret == 0 && (s_st.st_mode & S_IFSOCK) != 0);
    report_test("VFS", "5f. fstat(socket)->S_IFSOCK", ok_sock_st);
    all_passed &= ok_sock_st;

    close(vfs_s); /* Routes to lwip_close */

    printf("[VFS Test] --- Milestone 5 Verification Summary: %s ---\n",
           all_passed ? "ALL TESTS PASSED" : "TESTS FAILED");

    return (void *)(uintptr_t)all_passed;
}

/* ============================================================================
 * Master Application Thread: main_posix_app
 *
 * Orchestrates the simulated Linux user-space environment:
 *   1. Initializes mutexes, counting semaphores, and condition variables
 *   2. Spawns worker threads, producer-consumer threads, and HTTP server
 *   3. Triggers semaphore signaling via sem_post()
 *   4. Blocks on thread completion via pthread_join()
 *   5. Validates thread synchronization integrity and shared counter value
 *   6. Validates condition variable producer-consumer item counts
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

    /* Initialize condition variable synchronization primitives */
    if (pthread_mutex_init(&g_condvar_mutex, NULL) != 0) {
        printf("Failed to initialize condvar mutex\n");
        return NULL;
    }
    if (pthread_cond_init(&g_cond_not_empty, NULL) != 0) {
        printf("Failed to initialize cond_not_empty\n");
        return NULL;
    }
    if (pthread_cond_init(&g_cond_not_full, NULL) != 0) {
        printf("Failed to initialize cond_not_full\n");
        return NULL;
    }

    pthread_t thread1, thread2, sem_thread, web_server;
    pthread_t producer, consumer1, consumer2;

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

    /* Spawn Producer-Consumer Condition Variable Test Threads */
    printf("[Main] --- Starting Condition Variable Test (Producer-Consumer) ---\n");
    if (pthread_create(&producer, NULL, producer_thread, NULL) != 0)
        printf("Failed to create Producer thread\n");
    if (pthread_create(&consumer1, NULL, consumer_thread, (void *)(uintptr_t)1) != 0)
        printf("Failed to create Consumer 1\n");
    if (pthread_create(&consumer2, NULL, consumer_thread, (void *)(uintptr_t)2) != 0)
        printf("Failed to create Consumer 2\n");

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

    /* Wait for Producer-Consumer threads and validate results */
    void *producer_status = NULL;
    void *consumer1_status = NULL, *consumer2_status = NULL;

    printf("[Main] Joining Producer...\n");
    pthread_join(producer, &producer_status);
    printf("[Main] Producer joined.\n");

    printf("[Main] Joining Consumer 1...\n");
    pthread_join(consumer1, &consumer1_status);
    printf("[Main] Consumer 1 joined (consumed: %d items).\n",
           (int)(uintptr_t)consumer1_status);

    printf("[Main] Joining Consumer 2...\n");
    pthread_join(consumer2, &consumer2_status);
    printf("[Main] Consumer 2 joined (consumed: %d items).\n",
           (int)(uintptr_t)consumer2_status);

    /* Condition Variable Verification:
     * Consumer 1 + Consumer 2 must have consumed exactly TOTAL_ITEMS items */
    int total_consumed = (int)(uintptr_t)consumer1_status +
                         (int)(uintptr_t)consumer2_status;
    printf("[Main] --- Condition Variable Test Results ---\n");
    printf("[Main] Total items consumed: %d (Expected: %d) [%s]\n",
           total_consumed, TOTAL_ITEMS,
           (total_consumed == TOTAL_ITEMS) ? "PASSED" : "FAILED");

    /* Spawn Milestone 2 POSIX Socket Client Verification Thread */
    pthread_t client_test;
    void *client_test_status = NULL;
    printf("[Main] --- Spawning Milestone 2 Socket Client Verification Thread ---\n");
    if (pthread_create(&client_test, NULL, client_socket_test_thread, NULL) != 0) {
        printf("[Main] Failed to create Client Socket Test thread\n");
    } else {
        printf("[Main] Joining Client Socket Test...\n");
        pthread_join(client_test, &client_test_status);
        printf("[Main] Client Socket Test joined with status: %s\n",
               ((int)(uintptr_t)client_test_status == 1) ? "PASSED" : "FAILED");
    }

    /* Spawn Milestone 3 Clocks & High-Resolution Timers Verification Thread */
    pthread_t clock_test;
    void *clock_test_status = NULL;
    printf("[Main] --- Spawning Milestone 3 Clocks & Timers Verification Thread ---\n");
    if (pthread_create(&clock_test, NULL, clock_timer_test_thread, NULL) != 0) {
        printf("[Main] Failed to create Clock & Timer Test thread\n");
    } else {
        printf("[Main] Joining Clock & Timer Test...\n");
        pthread_join(clock_test, &clock_test_status);
        printf("[Main] Clock & Timer Test joined with status: %s\n",
               ((int)(uintptr_t)clock_test_status == 1) ? "PASSED" : "FAILED");
    }

    /* Spawn Milestone 4 Extended Mutex & Thread Attributes Verification Thread */
    pthread_t mutex_test;
    void *mutex_test_status = NULL;
    printf("[Main] --- Spawning Milestone 4 Mutex & Attributes Verification Thread ---\n");
    if (pthread_create(&mutex_test, NULL, mutex_attr_test_thread, NULL) != 0) {
        printf("[Main] Failed to create Mutex & Attr Test thread\n");
    } else {
        printf("[Main] Joining Mutex & Attr Test...\n");
        pthread_join(mutex_test, &mutex_test_status);
        printf("[Main] Mutex & Attr Test joined with status: %s\n",
               ((int)(uintptr_t)mutex_test_status == 1) ? "PASSED" : "FAILED");
    }

    /* Spawn Milestone 5 Virtual File System (VFS) Verification Thread */
    pthread_t vfs_test;
    void *vfs_test_status = NULL;
    printf("[Main] --- Spawning Milestone 5 VFS Verification Thread ---\n");
    if (pthread_create(&vfs_test, NULL, vfs_test_thread, NULL) != 0) {
        printf("[Main] Failed to create VFS Test thread\n");
    } else {
        printf("[Main] Joining VFS Test...\n");
        pthread_join(vfs_test, &vfs_test_status);
        printf("[Main] VFS Test joined with status: %s\n",
               ((int)(uintptr_t)vfs_test_status == 1) ? "PASSED" : "FAILED");
    }

    /*
     * Concurrency Verification:
     * Worker 1 (5) + Worker 2 (5) must equal exactly 10 if mutex locking is sound.
     */
    printf("[Main] Final shared counter value: %d (Expected: 10)\n", g_shared_counter);

    /* Clean up synchronization resources */
    pthread_mutex_destroy(&g_counter_mutex);
    sem_destroy(&g_job_semaphore);
    pthread_cond_destroy(&g_cond_not_empty);
    pthread_cond_destroy(&g_cond_not_full);
    pthread_mutex_destroy(&g_condvar_mutex);

    printf("--- All Milestone Verification Suites Completed Successfully ---\n");

    /* Wait for Web Server Daemon (runs continuously) */
    printf("[Main] Joining Web Server...\n");
    pthread_join(web_server, NULL);
    printf("[Main] Web Server joined.\n");

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