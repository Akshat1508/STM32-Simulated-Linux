/*
 * ============================================================================
 * File: lwipopts.h
 * Description: LwIP Compile-Time Stack Configuration Options
 * Target Platform: ARM Cortex-M3 (QEMU MPS2 AN385 / STM32)
 *
 * This header customizes the LwIP TCP/IP stack behavior for multithreaded
 * operation under FreeRTOS on resource-constrained embedded microcontrollers:
 *   - Operating Mode: NO_SYS = 0 (Enables OS mode with sequential/socket APIs)
 *   - Socket Layer: LWIP_SOCKET = 1 (Enables standard BSD POSIX socket API)
 *   - Memory Pools: Tailored buffer pool sizes for MTU 1500 frames without fragmentation
 *   - Thread Priorities: Dedicated priority levels for TCPIP core daemon
 * ============================================================================
 */

#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

/* ============================================================================
 * Operating System & API Support Configuration
 * ============================================================================
 */

/* NO_SYS = 0: Enables multithreaded OS mode with FreeRTOS synchronization */
#define NO_SYS                      0

/* LWIP_SOCKET = 1: Enables POSIX BSD socket API wrappers (socket, bind, listen, accept, etc.) */
#define LWIP_SOCKET                 1

/* LWIP_NETCONN = 1: Enables sequential Netconn abstraction layer */
#define LWIP_NETCONN                1

/* SYS_LIGHTWEIGHT_PROT = 1: Enables critical section protection for memory allocations */
#define SYS_LIGHTWEIGHT_PROT        1

/* LWIP_TIMEVAL_PRIVATE = 0: Uses standard toolchain struct timeval from <sys/time.h> */
#define LWIP_TIMEVAL_PRIVATE        0

/* ============================================================================
 * Core Protocol Suite Features
 * ============================================================================
 */

#define ETH_PAD_SIZE                0    /* Ethernet header alignment padding bytes */
#define LWIP_IPV4                   1    /* Enables IPv4 protocol processing */
#define LWIP_TCP                    1    /* Enables TCP connection and state machine processing */
#define LWIP_UDP                    1    /* Enables UDP datagram processing */

/* ============================================================================
 * LwIP System Thread Stack & Priority Configuration
 * ============================================================================
 */

/* LwIP Core Message Dispatcher Task (tcpip_thread) */
#define TCPIP_THREAD_NAME           "TCPIP"
#define TCPIP_THREAD_STACKSIZE      2048                        /* Stack size in words (8 KB) */
#define TCPIP_THREAD_PRIO           (configMAX_PRIORITIES - 2)  /* High execution priority */

/* Default worker thread parameters spawned by sys_thread_new */
#define DEFAULT_THREAD_STACKSIZE    1024
#define DEFAULT_THREAD_PRIO         (tskIDLE_PRIORITY + 1)

/* ============================================================================
 * Dynamic Memory Allocation & Pool Sizing
 * ============================================================================
 */

#define MEM_ALIGNMENT               4           /* 32-bit (4-byte) alignment for Cortex-M */
#define MEM_SIZE                    (16 * 1024) /* 16 KB heap for LwIP dynamic mem allocations */

/* Number of Protocol Control Blocks (PCBs) and connection descriptors */
#define MEMP_NUM_PBUF               16          /* Number of memp pbuf descriptors */
#define MEMP_NUM_RAW_PCB            4           /* Raw IP PCBs */
#define MEMP_NUM_UDP_PCB            4           /* UDP sockets */
#define MEMP_NUM_TCP_PCB            8           /* Active TCP connections */
#define MEMP_NUM_TCP_PCB_LISTEN     8           /* Listening TCP server sockets */
#define MEMP_NUM_TCP_SEG            16          /* Simultaneously queued TCP segments */
#define MEMP_NUM_SYS_TIMEOUT        15          /* Active software timer timeouts */
#define MEMP_NUM_NETBUF             8           /* Network buffers */
#define MEMP_NUM_NETCONN            8           /* Active Netconn structures */
#define MEMP_NUM_TCPIP_MSG_API      8           /* API messages in transit */
#define MEMP_NUM_TCPIP_MSG_INPKT    8           /* Input packet messages in transit */

/*
 * Packet Buffer Pool Configuration (PBUF_POOL)
 * PBUF_POOL_BUFSIZE: Sized to 1514 bytes so standard 1500-byte MTU Ethernet frames
 * fit entirely within a single contiguous buffer without chaining.
 */
#define PBUF_POOL_SIZE              8           /* Number of packet buffers in pool */
#define PBUF_POOL_BUFSIZE           1514        /* Capacity per buffer (Ethernet MTU + header) */

/* ============================================================================
 * TCP Window & Throughput Parameters
 * ============================================================================
 */

#define TCP_MSS                     1460        /* Maximum Segment Size (1500 MTU - 40 IP/TCP hdr) */
#define TCP_WND                     (4 * TCP_MSS) /* TCP Receive Window (5840 bytes) */
#define TCP_SND_BUF                 (4 * TCP_MSS) /* TCP Send Buffer (5840 bytes) */
#define TCP_SND_QUEUELEN            16          /* Maximum queued send segments */

/* ============================================================================
 * FreeRTOS Mailbox (Queue) Depths
 * ============================================================================
 */

#define TCPIP_MBOX_SIZE             16          /* Queue size for tcpip_thread inbox */
#define DEFAULT_RAW_RECVMBOX_SIZE   8           /* Receive queue size for RAW sockets */
#define DEFAULT_UDP_RECVMBOX_SIZE   8           /* Receive queue size for UDP sockets */
#define DEFAULT_TCP_RECVMBOX_SIZE   8           /* Receive queue size for TCP sockets */
#define DEFAULT_ACCEPTMBOX_SIZE     8           /* Queue size for incoming accept connections */

/* ============================================================================
 * Optional Subsystems Disabled for Flash/RAM Optimization
 * ============================================================================
 */

#define LWIP_DHCP                   0           /* Static IP used in QEMU; DHCP disabled */
#define LWIP_AUTOIP                 0           /* Auto-IP (APIPA) disabled */
#define LWIP_IGMP                   0           /* Multicast group management disabled */
#define LWIP_DNS                    0           /* DNS resolver client disabled */

#endif /* LWIP_LWIPOPTS_H */
