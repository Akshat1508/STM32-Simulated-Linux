OUTPUT_DIR := ./output
IMAGE := $(OUTPUT_DIR)/RTOSDemo.out

# The directory that contains the /source and /demo sub directories.
FREERTOS_ROOT = ./../../../..

CC = arm-none-eabi-gcc
LD = arm-none-eabi-gcc
SIZE = arm-none-eabi-size
MAKE = make

ifeq ($(PICOLIBC), 1)
    CFLAGS += --specs=picolibc.specs -DPICOLIBC_INTEGER_PRINTF_SCANF
endif
CFLAGS += -mthumb -mcpu=cortex-m3 -ffreestanding
CFLAGS += -g3 -Os -ffunction-sections -fdata-sections
#CFLAGS += -flto
CFLAGS += -Wall -Wextra -Wshadow -Wno-unused-value
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)" -MT $@
CFLAGS += $(INCLUDE_DIRS)

LDFLAGS = -T ./mps2_m3.ld
LDFLAGS += -Xlinker -Map=$(OUTPUT_DIR)/RTOSDemo.map
LDFLAGS += -Xlinker --gc-sections
#LDFLAGS += -Xlinker --print-gc-sections
ifneq ($(PICOLIBC), 1)
    LDFLAGS += -nostartfiles -specs=nano.specs -specs=nosys.specs # -specs=rdimon.specs
endif

#
# Kernel build.
#
KERNEL_DIR = $(FREERTOS_ROOT)/Source
KERNEL_PORT_DIR += $(KERNEL_DIR)/portable/GCC/ARM_CM3
INCLUDE_DIRS += -I$(KERNEL_DIR)/include \
				-I$(KERNEL_PORT_DIR)
VPATH += $(KERNEL_DIR) $(KERNEL_PORT_DIR) $(KERNEL_DIR)/portable/MemMang
SOURCE_FILES += $(KERNEL_DIR)/tasks.c
SOURCE_FILES += $(KERNEL_DIR)/list.c
SOURCE_FILES += $(KERNEL_DIR)/queue.c
SOURCE_FILES += $(KERNEL_DIR)/timers.c
SOURCE_FILES += $(KERNEL_DIR)/event_groups.c
SOURCE_FILES += $(KERNEL_DIR)/stream_buffer.c
SOURCE_FILES += $(KERNEL_DIR)/portable/MemMang/heap_4.c
SOURCE_FILES += $(KERNEL_DIR)/portable/GCC/ARM_CM3/port.c

#
# Common demo files for the "full" build, as opposed to the "blinky" build -
# these files are build by all the FreeRTOS kernel demos.
#
DEMO_ROOT = $(FREERTOS_ROOT)/Demo
COMMON_DEMO_FILES = $(DEMO_ROOT)/Common/Minimal
INCLUDE_DIRS += -I$(DEMO_ROOT)/Common/include
VPATH += $(COMMON_DEMO_FILES)
SOURCE_FILES += (COMMON_DEMO_FILES)/AbortDelay.c
SOURCE_FILES += (COMMON_DEMO_FILES)/BlockQ.c
SOURCE_FILES += (COMMON_DEMO_FILES)/blocktim.c
SOURCE_FILES += (COMMON_DEMO_FILES)/countsem.c
SOURCE_FILES += (COMMON_DEMO_FILES)/death.c
SOURCE_FILES += (COMMON_DEMO_FILES)/dynamic.c
SOURCE_FILES += (COMMON_DEMO_FILES)/EventGroupsDemo.c
SOURCE_FILES += (COMMON_DEMO_FILES)/GenQTest.c
SOURCE_FILES += (COMMON_DEMO_FILES)/integer.c
SOURCE_FILES += (COMMON_DEMO_FILES)/IntQueue.c
SOURCE_FILES += (COMMON_DEMO_FILES)/IntQueueTimer.c
SOURCE_FILES += (COMMON_DEMO_FILES)/IntSemTest.c
SOURCE_FILES += (COMMON_DEMO_FILES)/MessageBufferAMP.c
SOURCE_FILES += (COMMON_DEMO_FILES)/MessageBufferDemo.c
SOURCE_FILES += (COMMON_DEMO_FILES)/PollQ.c
SOURCE_FILES += (COMMON_DEMO_FILES)/QPeek.c
SOURCE_FILES += (COMMON_DEMO_FILES)/QueueOverwrite.c
SOURCE_FILES += (COMMON_DEMO_FILES)/QueueSet.c
SOURCE_FILES += (COMMON_DEMO_FILES)/QueueSetPolling.c
SOURCE_FILES += (COMMON_DEMO_FILES)/recmutex.c
SOURCE_FILES += (COMMON_DEMO_FILES)/semtest.c
SOURCE_FILES += (COMMON_DEMO_FILES)/StaticAllocation.c
SOURCE_FILES += (COMMON_DEMO_FILES)/StreamBufferDemo.c
SOURCE_FILES += (COMMON_DEMO_FILES)/StreamBufferInterrupt.c
SOURCE_FILES += (COMMON_DEMO_FILES)/TaskNotify.c
SOURCE_FILES += (COMMON_DEMO_FILES)/TaskNotifyArray.c
SOURCE_FILES += (COMMON_DEMO_FILES)/TimerDemo.c

#
# Application entry point.  main_blinky is self contained.  main_full builds
# the above common demo (and test) files too.
#
DEMO_PROJECT = $(DEMO_ROOT)/CORTEX_MPS2_QEMU_IAR_GCC
VPATH += $(DEMO_PROJECT)
INCLUDE_DIRS += -I$(DEMO_PROJECT) -I$(DEMO_PROJECT)/CMSIS
SOURCE_FILES += (DEMO_PROJECT)/main.c
SOURCE_FILES += (DEMO_PROJECT)/main_blinky.c
SOURCE_FILES += (DEMO_PROJECT)/main_full.c
SOURCE_FILES += ./startup_gcc.c
SOURCE_FILES += ./RegTest.c
# Lightweight print formatting to use in place of the heavier GCC equivalent.
SOURCE_FILES += ./printf-stdarg.c

#
# Lightweight IP (LwIP) Stack
#
LWIP_DIR = $(FREERTOS_ROOT)/Demo/Common/ethernet/lwip-1.4.0
LWIP_SRC = $(LWIP_DIR)/src
ETHERNET_DRIVER_DIR = $(FREERTOS_ROOT)/../FreeRTOS-Plus/Source/FreeRTOS-Plus-TCP/source/portable/NetworkInterface/MPS2_AN385/ether_lan9118

INCLUDE_DIRS += -I$(LWIP_SRC)/include \
                -I$(LWIP_SRC)/include/ipv4 \
                -I$(LWIP_SRC)/include/compat/posix \
                -I$(ETHERNET_DRIVER_DIR)

VPATH += $(LWIP_SRC)/core \
         $(LWIP_SRC)/core/ipv4 \
         $(LWIP_SRC)/api \
         $(LWIP_SRC)/netif \
         $(ETHERNET_DRIVER_DIR) \
         $(DEMO_PROJECT)

SOURCE_FILES += $(LWIP_SRC)/core/init.c \
                $(LWIP_SRC)/core/def.c \
                $(LWIP_SRC)/core/dns.c \
                $(LWIP_SRC)/core/mem.c \
                $(LWIP_SRC)/core/memp.c \
                $(LWIP_SRC)/core/netif.c \
                $(LWIP_SRC)/core/pbuf.c \
                $(LWIP_SRC)/core/raw.c \
                $(LWIP_SRC)/core/stats.c \
                $(LWIP_SRC)/core/sys.c \
                $(LWIP_SRC)/core/tcp.c \
                $(LWIP_SRC)/core/tcp_in.c \
                $(LWIP_SRC)/core/tcp_out.c \
                $(LWIP_SRC)/core/lwip_timers.c \
                $(LWIP_SRC)/core/udp.c \
                $(LWIP_SRC)/core/ipv4/autoip.c \
                $(LWIP_SRC)/core/ipv4/icmp.c \
                $(LWIP_SRC)/core/ipv4/igmp.c \
                $(LWIP_SRC)/core/ipv4/inet.c \
                $(LWIP_SRC)/core/ipv4/inet_chksum.c \
                $(LWIP_SRC)/core/ipv4/ip.c \
                $(LWIP_SRC)/core/ipv4/ip_addr.c \
                $(LWIP_SRC)/core/ipv4/ip_frag.c \
                $(LWIP_SRC)/netif/etharp.c \
                $(LWIP_SRC)/api/api_lib.c \
                $(LWIP_SRC)/api/api_msg.c \
                $(LWIP_SRC)/api/err.c \
                $(LWIP_SRC)/api/netbuf.c \
                $(LWIP_SRC)/api/netdb.c \
                $(LWIP_SRC)/api/netifapi.c \
                $(LWIP_SRC)/api/sockets.c \
                $(LWIP_SRC)/api/tcpip.c \
                $(DEMO_PROJECT)/sys_arch.c \
                $(DEMO_PROJECT)/ethernetif.c \
                $(ETHERNET_DRIVER_DIR)/smsc9220_eth_drv.c

# Percepio TraceRecorder (FreeRTOS-Plus-Trace)
TRACERECORDER_DIR = $(DEMO_ROOT)/../../FreeRTOS-Plus/Source/FreeRTOS-Plus-Trace
TRACERECORDER_CFG_DIR = $(DEMO_PROJECT)/TraceRecorderConfig
VPATH += $(TRACERECORDER_DIR)
VPATH += $(TRACERECORDER_DIR)/kernelports/FreeRTOS
VPATH += $(TRACERECORDER_DIR)/streamports/RingBuffer
INCLUDE_DIRS += -I$(TRACERECORDER_CFG_DIR)
INCLUDE_DIRS += -I$(TRACERECORDER_DIR)/include
INCLUDE_DIRS += -I$(TRACERECORDER_DIR)/kernelports/FreeRTOS/include
INCLUDE_DIRS += -I$(TRACERECORDER_DIR)/streamports/RingBuffer/include
SOURCE_FILES +=	(TRACERECORDER_DIR)/kernelports/FreeRTOS/trcKernelPort.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/streamports/RingBuffer/trcStreamPort.c
SOURCE_FILES += (TRACERECORDER_DIR)/trcAssert.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcCounter.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcDependency.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcDiagnostics.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcEntryTable.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcError.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcEvent.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcEventBuffer.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcExtension.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcHardwarePort.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcHeap.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcInternalEventBuffer.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcInterval.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcISR.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcMultiCoreEventBuffer.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcObject.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcPrint.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcRunnable.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcSnapshotRecorder.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcStackMonitor.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcStateMachine.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcStaticBuffer.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcStreamingRecorder.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcString.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcTask.c
SOURCE_FILES +=	(TRACERECORDER_DIR)/trcTimestamp.c

#Create a list of object files with the desired output directory path.
OBJS = $(SOURCE_FILES:%.c=%.o)
OBJS_NO_PATH = $(notdir $(OBJS))
OBJS_OUTPUT = $(OBJS_NO_PATH:%.o=$(OUTPUT_DIR)/%.o)

#Create a list of dependency files with the desired output directory path.
DEP_FILES := $(SOURCE_FILES:%.c=$(OUTPUT_DIR)/%.d)
DEP_FILES_NO_PATH = $(notdir $(DEP_FILES))
DEP_OUTPUT = $(DEP_FILES_NO_PATH:%.d=$(OUTPUT_DIR)/%.d)

all: $(IMAGE)

%.o : %.c
$(OUTPUT_DIR)/%.o : %.c $(OUTPUT_DIR)/%.d Makefile
	$(CC) $(CFLAGS) -c $< -o $@

$(IMAGE): ./mps2_m3.ld $(OBJS_OUTPUT) Makefile
	@echo ""
	@echo ""
	@echo "--- Final linking ---"
	@echo ""
	$(LD) $(CFLAGS) $(LDFLAGS) $(OBJS_OUTPUT) -o $(IMAGE)
	$(SIZE) $(IMAGE)

$(DEP_OUTPUT):
include $(wildcard $(DEP_OUTPUT))

clean:
	rm -f $(IMAGE) $(OUTPUT_DIR)/RTOSDemo.map $(OUTPUT_DIR)/*.o $(OUTPUT_DIR)/*.d

#use "make print-[VARIABLE_NAME] to print the value of a variable generated by
#this makefile.
print-%  : ; @echo $* = $($*)

.PHONY: all clean


