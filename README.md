# STM32 Simulated Linux

A POSIX compatibility layer running on top of the **FreeRTOS** real-time kernel to simulate a Linux execution environment on resource-constrained microcontrollers (like STM32 / ARM Cortex-M platforms). It compiles using `arm-none-eabi-gcc` and executes inside the **QEMU Emulator** (Cortex-M3 MPS2 AN385 platform).

---

## Key Capabilities & Features

### 1. Thread & Lifecycle Management (`pthread`)
* **Thread Creation**: `pthread_create` translates POSIX thread requests into FreeRTOS `xTaskCreate` calls. Dynamic memory allocations (`pvPortMalloc`/`vPortFree`) are used to safely pass thread routines and parameters.
* **Registry & Coordination**: Thread structures are tracked dynamically in a thread-safe registry (`g_thread_list`).
* **POSIX Compliance**: Fully supports `pthread_join` (blocking wait for a thread's completion with return value extraction), `pthread_exit` (for self-termination), `pthread_detach` (for resource reclamation of unjoined threads), and `pthread_self` (to query thread identities).

### 2. Synchronization Primitives
* **POSIX Mutexes (`pthread_mutex_t`)**: Maps standard locking APIs (`pthread_mutex_init`, `pthread_mutex_lock`, `pthread_mutex_unlock`, and `pthread_mutex_destroy`) directly to FreeRTOS Mutex Primitives for thread safety and race-condition prevention.
* **Custom Semaphores (`sem_t`)**: Implements counting semaphores (`sem_init`, `sem_wait`, `sem_post`, `sem_destroy`) to support inter-thread signaling, compensating for the lack of `<semaphore.h>` in standard newlib headers.

### 3. Compliant Timing & Delays
* Maps POSIX `sleep` (seconds) and `usleep` (microseconds) delays directly to FreeRTOS tick counts (`vTaskDelay`).

### 4. Networking & POSIX Sockets
* **LwIP Integration**: Integrates the lightweight LwIP TCP/IP stack running with full OS support (`NO_SYS = 0`). Memory footprint and buffers are optimized in `lwipopts.h` to fit inside the virtual STM32's constraints.
* **POSIX Socket Shim**: Maps standard Linux socket calls (`socket`, `bind`, `listen`, `accept`, `read`, `write`, `close`) directly to LwIP's built-in socket API.
* **Ethernet Driver Interface**: Bridges network interfaces in QEMU via a custom `ethernetif.c` driver mapping to the emulated SMSC9118 (LAN9118) Ethernet controller.
* **Interrupt-Driven Networking**: Configures NVIC IRQ 13 (Ethernet Interrupt) to handle incoming packet buffers asynchronously via a dedicated receiver task.

---

## Project Structure

* **`FreeRTOS/Source/`**: Core FreeRTOS kernel source code.
* **`FreeRTOS-Plus/`**: Supplementary packages, including the Percepio TraceRecorder and LwIP TCP/IP stack.
* **`FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/`**: Active target application folder.
  * **[main.c](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main.c)**: Redirection of `stdout` stream to UART0 register to pipe prints directly to the QEMU terminal window.
  * **[main_blinky.c](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c)**: The POSIX compatibility layer shim, worker threads demo, and simulated HTTP web server.
  * **[lwipopts.h](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/lwipopts.h)**: Configuration settings for LwIP.
  * **[sys_arch.c](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/sys_arch.c)**: LwIP OS adaptation layer mapping LwIP threads/queues to FreeRTOS.
  * **[ethernetif.c](FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/ethernetif.c)**: SMSC9118 Network controller driver for LwIP.
  * **`build/gcc/`**: Compilation Makefile, linker script (`mps2_m3.ld`), and exception/interrupt startup routines (`startup_gcc.c`).

---

## How to Build and Run

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
