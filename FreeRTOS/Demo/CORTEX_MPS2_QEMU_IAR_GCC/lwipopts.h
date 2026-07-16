#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

#define NO_SYS                      0
#define LWIP_SOCKET                 1
#define LWIP_NETCONN                1
#define SYS_LIGHTWEIGHT_PROT        1
#define LWIP_TIMEVAL_PRIVATE        0

#define ETH_PAD_SIZE                0
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1

#define TCPIP_THREAD_NAME           "TCPIP"
#define TCPIP_THREAD_STACKSIZE      2048
#define TCPIP_THREAD_PRIO           (configMAX_PRIORITIES - 2)

#define DEFAULT_THREAD_STACKSIZE    1024
#define DEFAULT_THREAD_PRIO         (tskIDLE_PRIORITY + 1)

/* Memory optimizations */
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (16 * 1024)
#define MEMP_NUM_PBUF               16
#define MEMP_NUM_RAW_PCB            4
#define MEMP_NUM_UDP_PCB            4
#define MEMP_NUM_TCP_PCB            8
#define MEMP_NUM_TCP_PCB_LISTEN     8
#define MEMP_NUM_TCP_SEG            16
#define MEMP_NUM_SYS_TIMEOUT        15
#define MEMP_NUM_NETBUF             8
#define MEMP_NUM_NETCONN            8
#define MEMP_NUM_TCPIP_MSG_API      8
#define MEMP_NUM_TCPIP_MSG_INPKT    8

#define PBUF_POOL_SIZE              8
#define PBUF_POOL_BUFSIZE           1514

#define TCP_MSS                     1460
#define TCP_WND                     (4 * TCP_MSS)
#define TCP_SND_BUF                 (4 * TCP_MSS)
#define TCP_SND_QUEUELEN            16

/* Mailbox sizes */
#define TCPIP_MBOX_SIZE             16
#define DEFAULT_RAW_RECVMBOX_SIZE   8
#define DEFAULT_UDP_RECVMBOX_SIZE   8
#define DEFAULT_TCP_RECVMBOX_SIZE   8
#define DEFAULT_ACCEPTMBOX_SIZE     8

/* Disabling features we don't need */
#define LWIP_DHCP                   0
#define LWIP_AUTOIP                 0
#define LWIP_IGMP                   0
#define LWIP_DNS                    0

#endif /* LWIP_LWIPOPTS_H */
