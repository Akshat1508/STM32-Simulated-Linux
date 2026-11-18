# Milestone 2: Outbound Client & Non-Blocking Sockets (Group C) Implementation & Verification Report

**Project:** STM32 Simulated Linux  
**Date:** November 18, 2026  
**Document:** `Documentations/work181126/milestone2/milestone2.md`  
**Milestone:** Group C — Outbound Client & Non-Blocking BSD Sockets  
**Target Architecture:** ARM Cortex-M3 (QEMU MPS2 AN385 / STM32)  
**RTOS Kernel:** FreeRTOS V202212.00  
**Network Stack:** LwIP 1.4.0 (OS mode with `tcpip_thread`)  
**Toolchain:** Arm GNU Toolchain 15.3.Rel2 (`arm-none-eabi-gcc`)  

---

## 1. Executive Summary

Milestone 2 expands the **STM32 Simulated Linux** compatibility layer from **27 active APIs** to **34 active APIs** by implementing the **Group C Networking & Non-Blocking Sockets Subsystem**.

Prior to this milestone, socket networking was constrained to inbound server daemons operating in blocking mode (`socket`, `bind`, `listen`, `accept`, `read`, `write`, `close`). Microcontroller edge nodes, however, frequently act as **HTTP/MQTT/CoAP clients** transmitting sensor telemetry or querying remote backend services. Furthermore, single-threaded event loops require non-blocking I/O and descriptor multiplexing (`select`, `poll`, `fcntl`) to monitor multiple concurrent connections without dedicating an entire RTOS task stack to each socket.

All 7 Group C APIs were incrementally implemented, mapped to the underlying LwIP stack, cross-compiled under `arm-none-eabi-gcc`, and verified under `qemu-system-arm` on the Cortex-M3 platform. The test suite exercised full loopback client communication with the embedded HTTP server, validating socket option configuration, non-blocking flag toggling, synchronous multiplexing, and bidirectional payload transmission with 100% pass rates.

---

## 2. Technical Architecture & I/O Multiplexing Pipeline

```
              POSIX Application Thread (Client / Telemetry Task)
                                     │
      ┌──────────────────────────────┼──────────────────────────────┐
      │                              │                              │
      ▼                              ▼                              ▼
 ┌──────────┐                  ┌──────────┐                   ┌──────────┐
 │ connect  │                  │   send   │                   │   recv   │
 └────┬─────┘                  └────┬─────┘                   └────┬─────┘
      │                             │                              │
      │                             ▼                              │
      │                    ┌──────────────────┐                    │
      │                    │  fcntl / poll /  │                    │
      │                    │      select      │                    │
      │                    └────────┬─────────┘                    │
      │                             │                              │
      └──────────────────────┬──────┴──────────────────────────────┘
                             ▼
              ┌──────────────────────────────┐
              │   POSIX Compatibility Shim   │
              │       (posix_shim.c)         │
              └──────────────┬───────────────┘
                             │
                             ▼
              ┌──────────────────────────────┐
              │    LwIP Sequential Sockets   │
              │  (lwip_connect, lwip_send,   │
              │   lwip_recv, lwip_select)    │
              └──────────────┬───────────────┘
                             │
                             ▼
              ┌──────────────────────────────┐
              │   Netconn / TCP Core Layer   │
              │        (tcpip_thread)        │
              └──────────────┬───────────────┘
                             │
         ┌───────────────────┴───────────────────┐
         ▼                                       ▼
 ┌────────────────┐                      ┌────────────────┐
 │  Local Loopback│                      │ LAN9118 Driver │
 │ (127.0.0.1/TCP)│                      │  (SMSC9118 Eth)│
 └────────────────┘                      └────────────────┘
```

### 2.1 Multiplexing Architecture (`poll` -> `select` Translation)
LwIP 1.4.0 natively provides `lwip_select()` but lacks a POSIX-compliant `poll()` entry point. To maintain zero overhead and full POSIX compatibility:
1. `poll()` accepts an array of `struct pollfd` structures.
2. It iterates through the array, populating standard `fd_set` bitmasks (`read_fds`, `write_fds`, `except_fds`) according to the requested `events` (`POLLIN`, `POLLOUT`, `POLLPRI`).
3. It converts millisecond timeouts into `struct timeval` and delegates to `lwip_select()`.
4. Upon return, it inspects descriptor readiness via `FD_ISSET()` and populates `revents` with corresponding flags (`POLLIN`, `POLLOUT`, `POLLERR`), returning the count of active descriptors.

### 2.2 Loopback Routing & Self-Addressing
To enable intra-firmware client-server testing and inter-process loopback IPC:
- `LWIP_NETIF_LOOPBACK 1` and `LWIP_HAVE_LOOPIF 1` were enabled in [`lwipopts.h`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/lwipopts.h).
- LwIP automatically provisions a loopback interface (`127.0.0.1`) during `netif_init()`.
- Packets addressed to `127.0.0.1` or the primary interface `10.0.2.15` are diverted through `netif_loop_output()`, queuing packets directly to `tcpip_thread` via internal mailboxes without touching physical Ethernet MAC hardware.

---

## 3. Exhaustive List of Implemented APIs (7 Total)

### 1. `connect`
* **File Location**: [`posix_shim.c`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L841-L845)
* **Signature**: `int connect(int s, const struct sockaddr *name, socklen_t namelen)`
* **Underlying Primitive**: `lwip_connect()`
* **Mechanics**: Initiates active TCP 3-way handshakes to external or local endpoints, transitioning sockets from `UNCONNECTED` to `ESTABLISHED`. Enables edge telemetry transmission and HTTP/MQTT client operations.

### 2. `send`
* **File Location**: [`posix_shim.c`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L850-L854)
* **Signature**: `int send(int s, const void *dataptr, size_t size, int flags)`
* **Underlying Primitive**: `lwip_send()`
* **Mechanics**: Transmits payload data over connected sockets. Supports standard POSIX control flags, including `MSG_DONTWAIT` for non-blocking packet submission to TCP output queues.

### 3. `recv`
* **File Location**: [`posix_shim.c`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L859-L863)
* **Signature**: `int recv(int s, void *mem, size_t len, int flags)`
* **Underlying Primitive**: `lwip_recv()`
* **Mechanics**: Extracts incoming data from TCP receive mailboxes. Supports `MSG_DONTWAIT` (non-blocking retrieval) and `MSG_PEEK` (reading without consuming buffer data).

### 4. `setsockopt`
* **File Location**: [`posix_shim.c`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L868-L872)
* **Signature**: `int setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)`
* **Underlying Primitive**: `lwip_setsockopt()`
* **Mechanics**: Modifies socket parameters. Configured with `SO_REUSE` and `LWIP_SO_RCVTIMEO` in `lwipopts.h` to enable `SO_REUSEADDR` (fast address re-binding) and `SO_RCVTIMEO` (receive timeout safeguarding).

### 5. `fcntl`
* **File Location**: [`posix_shim.c`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L877-L889)
* **Signature**: `int fcntl(int fd, int cmd, ...)`
* **Underlying Primitive**: `lwip_fcntl()`
* **Mechanics**: Uses variable arguments to inspect (`F_GETFL`) and modify (`F_SETFL`) file status flags. Specifically supports toggling `O_NONBLOCK` on socket file descriptors to prevent threads from stalling indefinitely on empty I/O queues.

### 6. `select`
* **File Location**: [`posix_shim.c`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L894-L899)
* **Signature**: `int select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout)`
* **Underlying Primitive**: `lwip_select()`
* **Mechanics**: Provides synchronous event multiplexing across multiple socket descriptors with microsecond timeout resolution. Signals readiness when read data arrives, write buffers have space, or exceptional errors occur.

### 7. `poll`
* **File Location**: [`posix_shim.c`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/posix_shim.c#L904-L984)
* **Signature**: `int poll(struct pollfd *fds, nfds_t nfds, int timeout)`
* **Underlying Primitive**: Custom POSIX Translation Layer mapping to `lwip_select()`
* **Mechanics**: Translates arrays of `struct pollfd` into `fd_set` bitmasks, executes descriptor monitoring, and converts read/write/error readiness back into `POLLIN`, `POLLOUT`, and `POLLERR` event flags.

---

## 4. Incremental Git Commit History

In accordance with project best practices, each API and configuration feature was implemented and committed as an atomic, standalone unit:

| Commit Hash | Message | Description |
| :--- | :--- | :--- |
| `f571c1e7` | `feat(network): enable SO_REUSE and SO_RCVTIMEO in lwipopts` | Enabled socket option macros in kernel network configuration |
| `655cc049` | `feat(network): implement connect API for outbound client sockets` | Added `connect()` mapping to `lwip_connect` |
| `36fda4e4` | `feat(network): implement send API with flag support` | Added `send()` mapping with `MSG_DONTWAIT` support |
| `8d855023` | `feat(network): implement recv API with flag support` | Added `recv()` mapping with buffer extraction |
| `b857c5c5` | `feat(network): implement setsockopt API for socket configuration` | Added `setsockopt()` supporting `SO_REUSEADDR` & `SO_RCVTIMEO` |
| `f8a43815` | `feat(network): implement fcntl API for non-blocking socket flag control` | Added `fcntl()` supporting `F_GETFL`/`F_SETFL` and `O_NONBLOCK` |
| `31a088fd` | `feat(network): implement select API for I/O event multiplexing` | Added `select()` mapping to `lwip_select` |
| `b5fcfe58` | `feat(network): implement poll API via select multiplexing` | Added POSIX `poll()` translating `pollfd` arrays to `fd_set` |
| `e0d4c043` | `feat(network): enable netif loopback and loopif in lwipopts` | Enabled `LWIP_NETIF_LOOPBACK` & `LWIP_HAVE_LOOPIF` |
| `647d9974` | `test(network): add Milestone 2 outbound client and non-blocking I/O test suite` | Integrated multi-threaded client verification suite in `main_blinky.c` |

---

## 5. Verification Suite & Test Design

The test suite in [`main_blinky.c`](../../../FreeRTOS/Demo/CORTEX_MPS2_QEMU_IAR_GCC/main_blinky.c#L380-L530) tests all 7 APIs end-to-end within the FreeRTOS multitasking environment:

```
[Web Server Thread (Port 80)]                [Client Test Thread]
             │                                        │
    1. Bind & Listen                                  │
             │                                        │
             │                             2. socket(AF_INET, SOCK_STREAM)
             │                             3. setsockopt(SO_REUSEADDR)
             │                             4. setsockopt(SO_RCVTIMEO)
             │                             5. fcntl(F_SETFL, O_NONBLOCK)
             │                             6. fcntl(F_SETFL, blocking)
             │                                        │
             │◄───────── TCP Handshake ───────────────┤ 7. connect("127.0.0.1:80")
    8. accept() returns client_fd                     │
             │                                        │
             │◄───────── "GET / HTTP/1.1\r\n..." ─────┤ 9. send()
   10. read() request payload                         │
             │                                        ├─► 11. select() -> Read Ready
   12. write() "HTTP/1.1 200 OK..." ─────────────────►│   12. poll()   -> POLLIN
   13. close(client_fd)                               │
             │                                        ├─► 13. recv() -> "200 OK"
             │                                        │   14. close(sock)
             ▼                                        ▼
    (Remains listening)                      (Exits with ALL PASSED)
```

### Tested Invariants:
1. **Socket Instantiation**: Verified `socket()` returns valid, non-negative file descriptors.
2. **Configuration Options**: Verified `setsockopt()` accepts `SO_REUSEADDR` and `SO_RCVTIMEO` without `EINVAL`.
3. **Descriptor Flag Manipulation**: Tested `fcntl(F_GETFL)` and `fcntl(F_SETFL, O_NONBLOCK)`, confirming non-blocking flags persist in the descriptor state.
4. **Outbound Handshake**: Successfully completed active TCP connect to local web server on `127.0.0.1:80`.
5. **Payload Transmission**: Sent standard HTTP GET request via `send()`, confirming byte counts match.
6. **I/O Readiness Multiplexing**: Verified both `select()` and `poll()` accurately trigger on pending incoming data.
7. **Payload Ingestion**: Read response via `recv()`, validating presence of `"HTTP/1.1 200 OK"`.

---

## 6. Empirical Verification & Test Results

### 6.1 Binary Footprint Analysis (`arm-none-eabi-size`)

The binary was compiled using `arm-none-eabi-gcc` 15.3.Rel2 with `-Os` size optimization:

```text
arm-none-eabi-size ./output/RTOSDemo.out
   text    data     bss     dec     hex filename
  75920     226  155586  231732   38934 ./output/RTOSDemo.out
```

* **Instruction FLASH Memory (`text`)**: **75.9 KB** (75,920 B). Remains within the constrained MCU FLASH budget ($\le 80$ KB).
* **Initialized Global Variables (`data`)**: **0.2 KB** (226 B).
* **Uninitialized RAM (`bss`)**: **151.9 KB** (155,586 B), consisting of FreeRTOS 100 KB heap (`heap_4.c`) and LwIP TCP/IP packet pools.

### 6.2 QEMU Execution Trace

Execution was verified under `qemu-system-arm` on the Cortex-M3 MPS2-AN385 platform:

```text
--- Booting Simulated Linux Environment ---
[Main] --- Starting Condition Variable Test (Producer-Consumer) ---
[Worker 1] Started. Incrementing counter 5 times...
[Worker 2] Started. Incrementing counter 5 times...
[Sem Worker] Waiting for semaphore...
[Producer] Started. Producing 8 items...
[Producer] Produced item 1 (buffer: 1/4)
[Consumer 1] Started. Waiting for items...
[Consumer 1] Consumed item 1 (buffer: 0/4)
[Consumer 2] Started. Waiting for items...
[Producer] Produced item 2 (buffer: 1/4)
[Consumer 2] Consumed item 2 (buffer: 0/4)
[Producer] Produced item 3 (buffer: 1/4)
[Consumer 1] Consumed item 3 (buffer: 0/4)
[Producer] Produced item 4 (buffer: 1/4)
[Consumer 2] Consumed item 4 (buffer: 0/4)
[Producer] Produced item 5 (buffer: 1/4)
[Consumer 1] Consumed item 5 (buffer: 0/4)
[Producer] Produced item 6 (buffer: 1/4)
[Consumer 2] Consumed item 6 (buffer: 0/4)
[Producer] Produced item 7 (buffer: 1/4)
[Consumer 1] Consumed item 7 (buffer: 0/4)
[Producer] Produced item 8 (buffer: 1/4)
[Consumer 2] Consumed item 8 (buffer: 0/4)
[Producer] All 8 items produced. Exiting.
[Consumer 1] Finished. Consumed 4 items total.
[Consumer 2] Finished. Consumed 4 items total.
[Worker 1] Finished.
[Worker 2] Finished.
[Main] Posting to semaphore...
[Main] Joining Worker 1...
[Main] Worker 1 joined with status: 1
[Main] Joining Worker 2...
[Main] Worker 2 joined with status: 2
[Main] Joining Semaphore Worker...
[Sem Worker] Semaphore received! Running task...
[Web Server] LwIP Initialized. IP address: 10.0.2.15
[Web Server] Listening on port 80...
[Sem Worker] Task completed. Exiting.
[Main] Semaphore Worker joined.
[Main] Joining Producer...
[Main] Producer joined.
[Main] Joining Consumer 1...
[Main] Consumer 1 joined (consumed: 4 items).
[Main] Joining Consumer 2...
[Main] Consumer 2 joined (consumed: 4 items).
[Main] --- Condition Variable Test Results ---
[Main] Total items consumed: 8 (Expected: 8) [PASSED]
[Main] --- Spawning Milestone 2 Socket Client Verification Thread ---
[Main] Joining Client Socket Test...
[Client Test] --- Starting Milestone 2 Socket Client Verification ---
[Client Test] 1. socket() created fd: 1 [PASSED]
[Client Test] 2a. setsockopt(SO_REUSEADDR) [PASSED]
[Client Test] 2b. setsockopt(SO_RCVTIMEO) [PASSED]
[Client Test] 3. fcntl(F_GETFL / F_SETFL, O_NONBLOCK) [PASSED]
[Client Test] Connecting to 127.0.0.1:80...
[Client Test] 4. connect() to HTTP server [PASSED]
[Web Server] Client connected from 127.0.0.1:49153
[Client Test] 5. send() transmitted 54 bytes [PASSED]
[Client Test] 6. select() detected read readiness [PASSED]
[Client Test] 7. poll() detected POLLIN event [PASSED]
[Client Test] 8. recv() received 255 bytes ('HTTP/1.1 200 OK') [PASSED]
[Client Test] --- Milestone 2 Sockets Verification Summary: ALL TESTS PASSED ---
[Main] Client Socket Test joined with status: PASSED
[Main] Final shared counter value: 10 (Expected: 10)
--- All Milestone Verification Suites Completed Successfully ---
[Main] Joining Web Server...
```

---

## 7. Status & Next Steps

* **Current Status**: **Milestone 2 Complete & Verified**.
* **Active APIs in Codebase**: **34 APIs** (22 Tier 1 + 5 Condition Variable APIs + 7 Group C Networking APIs).
* **Next Target**: **Milestone 3 (Group B — Dynamic Thread Configuration & Robust Mutex Attributes)**:
  * `pthread_attr_init`, `pthread_attr_destroy`: Dynamic thread attribute lifecycle.
  * `pthread_attr_setstacksize`, `pthread_attr_getstacksize`: Custom per-thread stack allocation.
  * `pthread_attr_setschedparam`, `pthread_attr_getschedparam`: Thread priority configuration.
  * `pthread_mutexattr_init`, `pthread_mutexattr_destroy`: Mutex attribute lifecycle.
  * `pthread_mutexattr_settype`, `pthread_mutexattr_gettype`: Mutex type selection (`PTHREAD_MUTEX_NORMAL`, `PTHREAD_MUTEX_RECURSIVE`, `PTHREAD_MUTEX_ERRORCHECK`).
