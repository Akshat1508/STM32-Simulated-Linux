# STM32 Simulated Linux

A POSIX compatibility layer running on top of the **FreeRTOS** real-time kernel and **LwIP** TCP/IP stack to simulate a Linux execution environment on resource-constrained microcontrollers (such as STM32 / ARM Cortex-M platforms). It cross-compiles using `arm-none-eabi-gcc` and executes inside the **QEMU Emulator** (Cortex-M3 MPS2 AN385 platform).

---

## 🎯 Ultimate Project Goal

The primary vision of this project is to construct a **lightweight, POSIX-compliant environment on ARM Cortex-M microcontrollers** without requiring a Hardware Memory Management Unit (MMU) or a heavy Linux kernel image. 

By translating standard UNIX/Linux system calls into native real-time OS primitives and embedded network stacks, developers can write, port, and execute standard multi-threaded C applications, synchronization patterns, and network server daemons directly on bare-metal microcontroller hardware or QEMU.

---

## 🚀 Key Achievements So Far

* **POSIX Thread Lifecycle Management**: Fully operational POSIX thread creation (`pthread_create`), joining (`pthread_join`), self-termination (`pthread_exit`), thread detaching (`pthread_detach`), and thread identity querying (`pthread_self`), backed by a thread-safe global task registry (`g_thread_list`).
* **Synchronization Primitives**: Integrated POSIX recursive/standard mutual exclusion locks (`pthread_mutex_*`) and a custom counting semaphore library (`sem_*`) supporting thread synchronization and race-condition prevention.
* **Compliant Delays & Timing**: Mapped POSIX timing delays (`sleep`, `usleep`) directly to FreeRTOS kernel scheduler ticks (`vTaskDelay`).
* **BSD Socket Networking**: Embedded BSD socket abstraction layer (`socket`, `bind`, `listen`, `accept`, `read`, `write`, `close`) integrated with the LwIP TCP/IP stack in OS mode.
* **QEMU Interrupt-Driven Ethernet Driver**: Custom SMSC9118 (LAN9118) Ethernet hardware driver (`ethernetif.c`) processing incoming network packets via NVIC IRQ 13 interrupts inside QEMU.
* **Demonstration POSIX HTTP Web Server**: Successfully boots and serves HTML content over virtual TCP port 80 (forwarded to host port 8080) inside QEMU.
* **Ultra-Compact Footprint**: Achieved an instruction code footprint (`text`) of **~66 KB**, fitting comfortably within tight MCU FLASH limits.

---

## 🔮 Pending Scope & Future Directions

*(General Overview of Ongoing Development)*

* **Advanced Inter-Thread Signalling**: Expanding synchronization mechanisms to support condition-based waiting and multi-task event notifications.
* **High-Resolution Clock Systems**: Enhancing system clock querying, timestamp generation, and fine-grained timer operations.
* **Non-Blocking Network I/O & Multiplexing**: Extending socket flag controls, non-blocking operation modes, and multi-socket event monitoring for concurrent connections.
* **Virtual I/O Abstractions**: Exploring a lightweight file descriptor mapping layer (VFS) to seamlessly route standard I/O streams across console UARTs, sockets, and memory buffers.

---

## 📁 Project Structure

* **`FreeRTOS/Source/`**: Core FreeRTOS kernel source code.
* **`FreeRTOS-Plus/`**: Supplementary packages, including LwIP TCP/IP stack and Percepio TraceRecorder.
* **`FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/`**: Active target application folder.
  * **[main.c](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main.c)**: Redirection of `stdout` stream to UART0 register to pipe prints directly to the QEMU terminal window.
  * **[posix_shim.h](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.h)** & **[posix_shim.c](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c)**: Decoupled POSIX compatibility layer shim mapping threads, mutexes, counting semaphores, and timing to FreeRTOS primitives.
  * **[main_blinky.c](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c)**: Worker threads demo application and simulated HTTP web server.
  * **[lwipopts.h](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/lwipopts.h)**: Configuration settings for LwIP.
  * **[sys_arch.c](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/sys_arch.c)**: LwIP OS adaptation layer mapping LwIP threads/queues to FreeRTOS.
  * **[ethernetif.c](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/ethernetif.c)**: SMSC9118 Network controller driver for LwIP.
  * **`build/gcc/`**: Compilation Makefile, linker script (`mps2_m3.ld`), and exception/interrupt startup routines (`startup_gcc.c`).
* **[API_TRANSLATION_ROADMAP.md](API_TRANSLATION_ROADMAP.md)**: Exhaustive breakdown of translated APIs, compatibility tiers, and detailed technical expansion specifications.

---

## 🛠️ How to Build and Run

### Prerequisites
Make sure you have the following tools installed and available on your PATH:
* **GNU ARM Toolchain**: `arm-none-eabi-gcc` and `arm-none-eabi-size`.
* **QEMU System Emulator**: `qemu-system-arm`.
* **GNU Make**: Standard build utility.

### Step 1: Compile the Project
Create the output directory if it does not exist, then compile the binary:
```bash
# Navigate to build directory
cd FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/build/gcc

# Create target output folder
mkdir -p output

# Build the binary
make clean all
```

### Step 2: Execute in QEMU
Run the following command to boot the simulated device in QEMU with NAT user-mode networking and host port-forwarding (which forwards host port `8080` to virtual port `80` inside QEMU):
```bash
qemu-system-arm -machine mps2-an385 -cpu cortex-m3 \
  -kernel output/RTOSDemo.out \
  -monitor none -nographic -serial stdio \
  -netdev user,id=mynet0,hostfwd=tcp::8080-:80 \
  -net nic,model=lan9118,netdev=mynet0
```
*(To exit the QEMU emulator terminal, press `Ctrl + A` then release and press `X`)*.

### Step 3: Test the POSIX Socket Web Server
Once the boot sequence logs `[Web Server] Listening on port 80...`, you can query the simulated Linux web server from your host machine's terminal:
```bash
curl http://localhost:8080
```

**Expected Response:**
```html
<!DOCTYPE html>
<html>
<head><title>STM32 Simulated Linux</title></head>
<body>
<h1>Hello from STM32 Simulated Linux!</h1>
<p>This web page is served from a simulated POSIX socket layer running on FreeRTOS inside QEMU.</p>
</body>
</html>
```
