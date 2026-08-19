# ============================================================================
# Makefile — GCC Build System Configuration for STM32 Simulated Linux
# Target Platform: ARM Cortex-M3 (QEMU MPS2 AN385 Platform)
# Toolchain: GNU Arm Embedded Toolchain (arm-none-eabi-gcc)
# ============================================================================

# Output binary file and build directory paths
OUTPUT_DIR := ./output
IMAGE := $(OUTPUT_DIR)/RTOSDemo.out

# Root relative path to FreeRTOS source tree
FREERTOS_ROOT = ./../../../..

# Cross-compilation toolchain executables
CC   = arm-none-eabi-gcc
LD   = arm-none-eabi-gcc
SIZE = arm-none-eabi-size
MAKE = make

# ============================================================================
# Compiler Flags Configuration (CFLAGS)
# Target: ARM Cortex-M3 Thumb-2 instruction set
# Optimization: -Os (Size optimization for microcontroller Flash limits)
# ============================================================================
ifeq ($(PICOLIBC), 1)
    CFLAGS += --specs=picolibc.specs -DPICOLIBC_INTEGER_PRINTF_SCANF
endif
CFLAGS += -mthumb -mcpu=cortex-m3 -ffreestanding
CFLAGS += -g3 -Os -ffunction-sections -fdata-sections
CFLAGS += -Wall -Wextra -Wshadow -Wno-unused-value
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)" -MT $@
CFLAGS += $(INCLUDE_DIRS)

# ============================================================================
# Linker Flags Configuration (LDFLAGS)
# Uses custom linker script mps2_m3.ld (Flash: 4096K, SRAM: 8192K)
# Enables dead code stripping (--gc-sections) and newlib nano specs
# ============================================================================
LDFLAGS = -T ./mps2_m3.ld
LDFLAGS += -Xlinker -Map=$(OUTPUT_DIR)/RTOSDemo.map
LDFLAGS += -Xlinker --gc-sections
ifneq ($(PICOLIBC), 1)
    LDFLAGS += -nostartfiles -specs=nano.specs -specs=nosys.specs
endif

# ============================================================================
# 1. FreeRTOS Real-Time Kernel Core Sources
# Tasks, scheduler queues, software timers, lists, and heap_4 memory manager
# ============================================================================
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

# ============================================================================
# 2. Common FreeRTOS Demo & Test Suite Files
# Built for comprehensive kernel verification (used when main_full is active)
# ============================================================================
DEMO_ROOT = $(FREERTOS_ROOT)/Demo
COMMON_DEMO_FILES = $(DEMO_ROOT)/Common/Minimal
INCLUDE_DIRS += -I$(DEMO_ROOT)/Common/include
VPATH += $(COMMON_DEMO_FILES)
SOURCE_FILES += $(COMMON_DEMO_FILES)/AbortDelay.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/BlockQ.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/blocktim.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/countsem.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/death.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/dynamic.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/EventGroupsDemo.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/GenQTest.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/integer.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/IntQueue.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/IntQueueTimer.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/IntSemTest.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/MessageBufferAMP.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/MessageBufferDemo.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/PollQ.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/QPeek.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/QueueOverwrite.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/QueueSet.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/QueueSetPolling.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/recmutex.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/semtest.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/StaticAllocation.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/StreamBufferDemo.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/StreamBufferInterrupt.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/TaskNotify.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/TaskNotifyArray.c
SOURCE_FILES += $(COMMON_DEMO_FILES)/TimerDemo.c

# ============================================================================
# 3. Application & POSIX Compatibility Layer Sources
# Active simulated Linux target: main.c, main_blinky.c, posix_shim.c, startup
# ============================================================================
DEMO_PROJECT = $(DEMO_ROOT)/CORTEX_MPS2_QEMU_IAR_GCC
VPATH += $(DEMO_PROJECT)
INCLUDE_DIRS += -I$(DEMO_PROJECT) -I$(DEMO_PROJECT)/CMSIS
SOURCE_FILES += $(DEMO_PROJECT)/main.c
SOURCE_FILES += $(DEMO_PROJECT)/main_blinky.c
SOURCE_FILES += $(DEMO_PROJECT)/main_full.c
SOURCE_FILES += $(DEMO_PROJECT)/posix_shim.c
SOURCE_FILES += ./startup_gcc.c
SOURCE_FILES += ./RegTest.c
SOURCE_FILES += ./printf-stdarg.c

# ============================================================================
# 4. Lightweight IP (LwIP) Stack & Network Drivers
# LwIP 1.4.0 core protocols, OS glue (sys_arch.c), and SMSC9118 Ethernet driver
# ============================================================================
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

# ============================================================================
# 5. Percepio TraceRecorder Instrumentation (FreeRTOS-Plus-Trace)
# ============================================================================
TRACERECORDER_DIR = $(DEMO_ROOT)/../../FreeRTOS-Plus/Source/FreeRTOS-Plus-Trace
TRACERECORDER_CFG_DIR = $(DEMO_PROJECT)/TraceRecorderConfig
VPATH += $(TRACERECORDER_DIR)
VPATH += $(TRACERECORDER_DIR)/kernelports/FreeRTOS
VPATH += $(TRACERECORDER_DIR)/streamports/RingBuffer
INCLUDE_DIRS += -I$(TRACERECORDER_CFG_DIR)
INCLUDE_DIRS += -I$(TRACERECORDER_DIR)/include
INCLUDE_DIRS += -I$(TRACERECORDER_DIR)/kernelports/FreeRTOS/include
INCLUDE_DIRS += -I$(TRACERECORDER_DIR)/streamports/RingBuffer/include
SOURCE_FILES += $(TRACERECORDER_DIR)/kernelports/FreeRTOS/trcKernelPort.c
SOURCE_FILES += $(TRACERECORDER_DIR)/streamports/RingBuffer/trcStreamPort.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcAssert.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcCounter.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcDependency.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcDiagnostics.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcEntryTable.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcError.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcEvent.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcEventBuffer.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcExtension.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcHardwarePort.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcHeap.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcInternalEventBuffer.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcInterval.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcISR.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcMultiCoreEventBuffer.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcObject.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcPrint.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcRunnable.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcSnapshotRecorder.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcStackMonitor.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcStateMachine.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcStaticBuffer.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcStreamingRecorder.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcString.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcTask.c
SOURCE_FILES += $(TRACERECORDER_DIR)/trcTimestamp.c

# ============================================================================
# Object & Dependency List Construction
# ============================================================================
OBJS = $(SOURCE_FILES:%.c=%.o)
OBJS_NO_PATH = $(notdir $(OBJS))
OBJS_OUTPUT = $(OBJS_NO_PATH:%.o=$(OUTPUT_DIR)/%.o)

DEP_FILES := $(SOURCE_FILES:%.c=$(OUTPUT_DIR)/%.d)
DEP_FILES_NO_PATH = $(notdir $(DEP_FILES))
DEP_OUTPUT = $(DEP_FILES_NO_PATH:%.d=$(OUTPUT_DIR)/%.d)

# ============================================================================
# Build Rules & Targets
# ============================================================================

# Default target: compile and link firmware binary
all: $(IMAGE)

# Compilation rule: C source to object file with header dependency generation
%.o : %.c
$(OUTPUT_DIR)/%.o : %.c $(OUTPUT_DIR)/%.d Makefile
	$(CC) $(CFLAGS) -c $< -o $@

# Final linking rule: Links all objects using linker script mps2_m3.ld
$(IMAGE): ./mps2_m3.ld $(OBJS_OUTPUT) Makefile
	@echo ""
	@echo ""
	@echo "--- Final linking ---"
	@echo ""
	$(LD) $(CFLAGS) $(LDFLAGS) $(OBJS_OUTPUT) -o $(IMAGE)
	$(SIZE) $(IMAGE)

# Include generated header dependency files (.d)
$(DEP_OUTPUT):
include $(wildcard $(DEP_OUTPUT))

# Clean build artifacts rule
clean:
	rm -f $(IMAGE) $(OUTPUT_DIR)/RTOSDemo.map $(OUTPUT_DIR)/*.o $(OUTPUT_DIR)/*.d

# Debug helper to inspect variable values: make print-CFLAGS
print-% : ; @echo $* = $($*)

.PHONY: all clean
