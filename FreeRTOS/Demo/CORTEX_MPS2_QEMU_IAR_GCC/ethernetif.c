#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include "lwip/stats.h"
#include "lwip/snmp.h"
#include "netif/etharp.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "SMM_MPS2.h"
#include "smsc9220_eth_drv.h"
#include "smsc9220_emac_config.h"
#include <string.h>

#define nwNVIC_ISER          ( *( ( volatile uint32_t * ) 0xE000E100UL ) )
#define nwNVIC_ICER          ( *( ( volatile uint32_t * ) 0xE000E180UL ) )

static const struct smsc9220_eth_dev_cfg_t SMSC9220_ETH_DEV_CFG = {
    .base = SMSC9220_BASE
};

static struct smsc9220_eth_dev_data_t SMSC9220_ETH_DEV_DATA = {
    .state = 0
};

static const struct smsc9220_eth_dev_t SMSC9220_ETH_DEV = {
    &SMSC9220_ETH_DEV_CFG,
    &SMSC9220_ETH_DEV_DATA
};

static TaskHandle_t xRxTaskHandle = NULL;

static void prvWait_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void ethernetif_input_task(void *pvParameters) {
    struct netif *netif = (struct netif *)pvParameters;
    const struct smsc9220_eth_dev_t *dev = &SMSC9220_ETH_DEV;

    for (;;) {
        // Wait for interrupt notification
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t packet_len;
        while ((packet_len = smsc9220_peek_next_packet_size(dev)) > 0) {
            struct pbuf *p = pbuf_alloc(PBUF_RAW, packet_len, PBUF_POOL);
            if (p != NULL) {
                // Since our PBUF_POOL_BUFSIZE (1514) is larger than standard MTU,
                // the allocated pbuf will be a single contiguous buffer (no chaining).
                uint32_t read_len = smsc9220_receive_by_chunks(dev, (char *)p->payload, packet_len);
                p->len = read_len;
                p->tot_len = read_len;

                if (netif->input(p, netif) != ERR_OK) {
                    pbuf_free(p);
                }
            } else {
                // Discard the packet if memory allocation failed to clear the controller's buffer
                char drop_buf[64];
                uint32_t remaining = packet_len;
                while (remaining > 0) {
                    uint32_t chunk = remaining > sizeof(drop_buf) ? sizeof(drop_buf) : remaining;
                    smsc9220_receive_by_chunks(dev, drop_buf, chunk);
                    remaining -= chunk;
                }
            }
        }

        // Re-enable interrupts
        smsc9220_enable_interrupt(dev, SMSC9220_INTERRUPT_RX_STATUS_FIFO_LEVEL);
    }
}

void EthernetISR(void) {
    const struct smsc9220_eth_dev_t *dev = &SMSC9220_ETH_DEV;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint32_t ulIRQStatus;
    const uint32_t ulRXFifoStatusIRQBit = 1UL << SMSC9220_INTERRUPT_RX_STATUS_FIFO_LEVEL;
    extern uint32_t get_irq_status(const struct smsc9220_eth_dev_t *dev);

    ulIRQStatus = get_irq_status(dev);

    if ((ulIRQStatus & ulRXFifoStatusIRQBit) != 0) {
        if (xRxTaskHandle != NULL) {
            vTaskNotifyGiveFromISR(xRxTaskHandle, &xHigherPriorityTaskWoken);
        }
        smsc9220_clear_interrupt(dev, SMSC9220_INTERRUPT_RX_STATUS_FIFO_LEVEL);
        smsc9220_disable_interrupt(dev, SMSC9220_INTERRUPT_RX_STATUS_FIFO_LEVEL);
    }

    smsc9220_clear_all_interrupts(dev);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void low_level_init(struct netif *netif) {
    const struct smsc9220_eth_dev_t *dev = &SMSC9220_ETH_DEV;
    const uint32_t ulEthernetIRQ = 13UL;

    // Set MAC address registers
    uint32_t ucMACLow = 0;
    uint32_t ucMACHigh = 0;
    memcpy(&ucMACLow, netif->hwaddr, 4);
    memcpy(&ucMACHigh, netif->hwaddr + 4, 2);

    smsc9220_init(dev, prvWait_ms);

    smsc9220_mac_regwrite(dev, SMSC9220_MAC_REG_OFFSET_ADDRL, ucMACLow);
    smsc9220_mac_regwrite(dev, SMSC9220_MAC_REG_OFFSET_ADDRH, ucMACHigh);

    // Disable interrupts in NVIC and device initially
    nwNVIC_ICER = (uint32_t)(1UL << (ulEthernetIRQ & 0x1FUL));
    smsc9220_disable_all_interrupts(dev);
    smsc9220_clear_all_interrupts(dev);

    // Set priority of Ethernet interrupt to be compatible with FreeRTOS syscall limit
    NVIC_SetPriority((IRQn_Type)ulEthernetIRQ, configMAX_SYSCALL_INTERRUPT_PRIORITY);

    smsc9220_set_fifo_level_irq(dev, SMSC9220_FIFO_LEVEL_IRQ_RX_STATUS_POS, SMSC9220_FIFO_LEVEL_IRQ_LEVEL_MIN);

    // Enable Rx interrupt sources
    smsc9220_enable_interrupt(dev, SMSC9220_INTERRUPT_RX_STATUS_FIFO_LEVEL);

    // Create receiver task
    xTaskCreate(ethernetif_input_task, "LWIP_RX", 1024, netif, configMAX_PRIORITIES - 3, &xRxTaskHandle);

    // Enable Ethernet IRQ in NVIC
    nwNVIC_ISER = (uint32_t)(1UL << (ulEthernetIRQ & 0x1FUL));
}

static err_t low_level_output(struct netif *netif, struct pbuf *p) {
    (void)netif;
    const struct smsc9220_eth_dev_t *dev = &SMSC9220_ETH_DEV;
    struct pbuf *q;
    bool is_start = true;
    uint32_t total_len = p->tot_len;

    for (q = p; q != NULL; q = q->next) {
        enum smsc9220_error_t err = smsc9220_send_by_chunks(dev, total_len, is_start, (const char *)q->payload, q->len);
        if (err != SMSC9220_ERROR_NONE) {
            return ERR_IF;
        }
        is_start = false;
    }
    return ERR_OK;
}

err_t ethernetif_init(struct netif *netif) {
    netif->name[0] = 'e';
    netif->name[1] = 'n';
    netif->output = etharp_output;
    netif->linkoutput = low_level_output;

    netif->hwaddr_len = 6;
    netif->hwaddr[0] = 0x00;
    netif->hwaddr[1] = 0x08;
    netif->hwaddr[2] = 0x29;
    netif->hwaddr[3] = 0x11;
    netif->hwaddr[4] = 0x22;
    netif->hwaddr[5] = 0x33;

    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP;

    low_level_init(netif);

    return ERR_OK;
}
