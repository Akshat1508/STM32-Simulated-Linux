/*
 * ============================================================================
 * File: ethernetif.c
 * Description: SMSC9118 / LAN9118 Ethernet Hardware Interface Driver for LwIP
 * Target Platform: ARM Cortex-M3 (QEMU MPS2 AN385 Platform)
 *
 * This module connects the LwIP TCP/IP stack to the physical SMSC9118 (LAN9118)
 * Ethernet controller inside QEMU. It implements:
 *   1. Hardware Initialization: Sets MAC address, MTU 1500, NVIC IRQ 13 priority.
 *   2. Interrupt Service Routine (EthernetISR): Handles hardware IRQ 13, clears
 *      interrupt flags, and wakes the packet processing task via FreeRTOS direct
 *      task notifications (vTaskNotifyGiveFromISR).
 *   3. Packet Reception (ethernetif_input_task): High-priority daemon task (LWIP_RX)
 *      retrieving raw frame bytes from hardware FIFO into LwIP pbuf structures.
 *   4. Packet Transmission (low_level_output): Streams pbuf payload chunks
 *      directly into SMSC9118 hardware TX FIFO registers.
 * ============================================================================
 */

/* LwIP Core & Network Interface Headers */
#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "netif/etharp.h"

/* FreeRTOS Kernel API */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ARM Cortex-M Hardware & Device Driver Headers */
#include "SMM_MPS2.h"
#include "smsc9220_eth_drv.h"
#include "smsc9220_emac_config.h"
#include <string.h>

/*
 * ARM Cortex-M Nested Vectored Interrupt Controller (NVIC) Hardware Registers
 * - nwNVIC_ISER: Interrupt Set Enable Register (0xE000E100)
 * - nwNVIC_ICER: Interrupt Clear Enable Register (0xE000E180)
 */
#define nwNVIC_ISER          ( *( ( volatile uint32_t * ) 0xE000E100UL ) )
#define nwNVIC_ICER          ( *( ( volatile uint32_t * ) 0xE000E180UL ) )

/* SMSC9220 Device Hardware Configuration Descriptor */
static const struct smsc9220_eth_dev_cfg_t SMSC9220_ETH_DEV_CFG = {
    .base = SMSC9220_BASE
};

/* SMSC9220 Driver Runtime State Data */
static struct smsc9220_eth_dev_data_t SMSC9220_ETH_DEV_DATA = {
    .state = 0
};

/* Global Device Handle passed to smsc9220 driver functions */
static const struct smsc9220_eth_dev_t SMSC9220_ETH_DEV = {
    &SMSC9220_ETH_DEV_CFG,
    &SMSC9220_ETH_DEV_DATA
};

/* Task handle of the high-priority LWIP_RX packet ingestion task */
static TaskHandle_t xRxTaskHandle = NULL;

/**
 * @brief Millisecond delay callback used by the smsc9220 hardware driver during reset.
 */
static void prvWait_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/**
 * @brief High-Priority Packet Processing Task (LWIP_RX).
 *
 * Blocks on ulTaskNotifyTake() until signaled by EthernetISR(). Once awakened,
 * peeks the incoming packet size, allocates a contiguous pbuf from the LwIP
 * memory pool, streams raw Ethernet frames from the hardware FIFO, and routes
 * them up the stack via netif->input().
 *
 * @param pvParameters Pointer to the active struct netif interface.
 */
static void ethernetif_input_task(void *pvParameters)
{
    struct netif *netif = (struct netif *)pvParameters;
    const struct smsc9220_eth_dev_t *dev = &SMSC9220_ETH_DEV;

    for (;;) {
        /* Block indefinitely waiting for hardware interrupt notification */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t packet_len;
        /* Process all frames currently queued in the hardware Rx FIFO */
        while ((packet_len = smsc9220_peek_next_packet_size(dev)) > 0) {
            /* Allocate packet buffer from PBUF_POOL (matching MTU size) */
            struct pbuf *p = pbuf_alloc(PBUF_RAW, packet_len, PBUF_POOL);
            if (p != NULL) {
                /*
                 * Stream packet bytes directly from SMSC9220 hardware FIFO
                 * into the allocated pbuf payload memory.
                 */
                uint32_t read_len = smsc9220_receive_by_chunks(dev, (char *)p->payload, packet_len);
                p->len = read_len;
                p->tot_len = read_len;

                /* Pass packet up to LwIP (etharp_input or ip_input) */
                if (netif->input(p, netif) != ERR_OK) {
                    pbuf_free(p); /* Reclaim pbuf on processing error */
                }
            } else {
                /*
                 * Memory allocation failed (pool exhausted): Flush and discard
                 * packet bytes from hardware FIFO to prevent controller stalls.
                 */
                char drop_buf[64];
                uint32_t remaining = packet_len;
                while (remaining > 0) {
                    uint32_t chunk = remaining > sizeof(drop_buf) ? sizeof(drop_buf) : remaining;
                    smsc9220_receive_by_chunks(dev, drop_buf, chunk);
                    remaining -= chunk;
                }
            }
        }

        /* Re-enable Rx status FIFO level interrupt on controller */
        smsc9220_enable_interrupt(dev, SMSC9220_INTERRUPT_RX_STATUS_FIFO_LEVEL);
    }
}

/**
 * @brief Hardware Interrupt Service Routine for Ethernet (NVIC IRQ 13).
 *
 * Triggered when the SMSC9118 asserts IRQ 13 upon receiving a network packet.
 * Clears interrupt flags, disables FIFO interrupts, notifies the LWIP_RX task,
 * and requests an immediate context switch if LWIP_RX has higher priority.
 */
void EthernetISR(void)
{
    const struct smsc9220_eth_dev_t *dev = &SMSC9220_ETH_DEV;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint32_t ulIRQStatus;
    const uint32_t ulRXFifoStatusIRQBit = 1UL << SMSC9220_INTERRUPT_RX_STATUS_FIFO_LEVEL;
    extern uint32_t get_irq_status(const struct smsc9220_eth_dev_t *dev);

    /* Read hardware interrupt status register */
    ulIRQStatus = get_irq_status(dev);

    /* Check if interrupt was caused by incoming Rx packet */
    if ((ulIRQStatus & ulRXFifoStatusIRQBit) != 0) {
        if (xRxTaskHandle != NULL) {
            /* Signal the deferred packet processing task */
            vTaskNotifyGiveFromISR(xRxTaskHandle, &xHigherPriorityTaskWoken);
        }
        /* Clear and disable Rx interrupt until task empties the FIFO */
        smsc9220_clear_interrupt(dev, SMSC9220_INTERRUPT_RX_STATUS_FIFO_LEVEL);
        smsc9220_disable_interrupt(dev, SMSC9220_INTERRUPT_RX_STATUS_FIFO_LEVEL);
    }

    /* Acknowledge all pending hardware interrupts */
    smsc9220_clear_all_interrupts(dev);

    /* Request context switch if a higher priority task was unblocked */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/**
 * @brief Low-level initialization of the SMSC9118 Ethernet hardware controller.
 *
 * @param netif Pointer to LwIP network interface structure.
 */
static void low_level_init(struct netif *netif)
{
    const struct smsc9220_eth_dev_t *dev = &SMSC9220_ETH_DEV;
    const uint32_t ulEthernetIRQ = 13UL; /* NVIC Position 13 */

    /* Extract MAC address and write to hardware registers */
    uint32_t ucMACLow = 0;
    uint32_t ucMACHigh = 0;
    memcpy(&ucMACLow, netif->hwaddr, 4);
    memcpy(&ucMACHigh, netif->hwaddr + 4, 2);

    /* Initialize physical controller and PHY */
    smsc9220_init(dev, prvWait_ms);

    smsc9220_mac_regwrite(dev, SMSC9220_MAC_REG_OFFSET_ADDRL, ucMACLow);
    smsc9220_mac_regwrite(dev, SMSC9220_MAC_REG_OFFSET_ADDRH, ucMACHigh);

    /* Disable interrupts in NVIC and device during initial configuration */
    nwNVIC_ICER = (uint32_t)(1UL << (ulEthernetIRQ & 0x1FUL));
    smsc9220_disable_all_interrupts(dev);
    smsc9220_clear_all_interrupts(dev);

    /*
     * Configure NVIC interrupt priority:
     * Must be equal to or lower priority than configMAX_SYSCALL_INTERRUPT_PRIORITY
     * to safely invoke FreeRTOS FromISR APIs.
     */
    NVIC_SetPriority((IRQn_Type)ulEthernetIRQ, configMAX_SYSCALL_INTERRUPT_PRIORITY);

    /* Configure Rx FIFO threshold to trigger IRQ immediately on packet arrival */
    smsc9220_set_fifo_level_irq(dev, SMSC9220_FIFO_LEVEL_IRQ_RX_STATUS_POS, SMSC9220_FIFO_LEVEL_IRQ_LEVEL_MIN);

    /* Enable Rx status FIFO interrupt */
    smsc9220_enable_interrupt(dev, SMSC9220_INTERRUPT_RX_STATUS_FIFO_LEVEL);

    /* Spawn dedicated high-priority packet receiver task */
    xTaskCreate(ethernetif_input_task,
                "LWIP_RX",
                1024,
                netif,
                configMAX_PRIORITIES - 3,
                &xRxTaskHandle);

    /* Enable Ethernet IRQ in ARM Cortex-M NVIC */
    nwNVIC_ISER = (uint32_t)(1UL << (ulEthernetIRQ & 0x1FUL));
}

/**
 * @brief Low-level network packet transmission routine.
 *
 * Called by LwIP whenever an IP/ARP packet needs to be sent out over the network.
 * Streams pbuf payload fragments directly into SMSC9118 TX FIFO registers.
 *
 * @param netif Pointer to LwIP network interface.
 * @param p     Packet buffer chain containing payload bytes to transmit.
 *
 * @return ERR_OK on successful transmission, or ERR_IF on hardware error.
 */
static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    const struct smsc9220_eth_dev_t *dev = &SMSC9220_ETH_DEV;
    struct pbuf *q;
    bool is_start = true;
    uint32_t total_len = p->tot_len;

    /* Iterate through all pbuf chunks in the packet chain */
    for (q = p; q != NULL; q = q->next) {
        enum smsc9220_error_t err = smsc9220_send_by_chunks(dev, total_len, is_start, (const char *)q->payload, q->len);
        if (err != SMSC9220_ERROR_NONE) {
            return ERR_IF;
        }
        is_start = false;
    }
    return ERR_OK;
}

/**
 * @brief Registers and initializes the LwIP Ethernet network interface.
 *
 * Sets MAC address, MTU, output function pointers, and brings up the interface.
 *
 * @param netif Pointer to the netif structure to initialize.
 *
 * @return ERR_OK on success.
 */
err_t ethernetif_init(struct netif *netif)
{
    netif->name[0] = 'e';
    netif->name[1] = 'n';
    netif->output = etharp_output;       /* Route IP packets through ARP */
    netif->linkoutput = low_level_output; /* Hardware TX driver */

    /* Configure MAC Address (00:08:29:11:22:33) */
    netif->hwaddr_len = 6;
    netif->hwaddr[0] = 0x00;
    netif->hwaddr[1] = 0x08;
    netif->hwaddr[2] = 0x29;
    netif->hwaddr[3] = 0x11;
    netif->hwaddr[4] = 0x22;
    netif->hwaddr[5] = 0x33;

    /* Maximum Transfer Unit (standard Ethernet MTU) */
    netif->mtu = 1500;

    /* Configure interface capability flags */
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP;

    /* Perform low-level hardware controller initialization */
    low_level_init(netif);

    return ERR_OK;
}
