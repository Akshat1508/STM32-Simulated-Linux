/*
 * ============================================================================
 * File: main.c
 * Description: Master Hardware Setup, Stdout Redirection & FreeRTOS Hook Handlers
 * Target Platform: ARM Cortex-M3 (MPS2 AN385 Emulator Model on QEMU)
 *
 * Core Responsibilities:
 *   1. Hardware Initialization: Configures MPS2 UART0 MMIO registers.
 *   2. Console I/O Redirection: Routes C standard printf() via __write()/_uart_putc()
 *      directly to UART0 data register (0x40004000UL) for QEMU stdio output.
 *   3. Application Dispatcher: Dispatches to main_blinky() (Simulated Linux mode).
 *   4. Exception & Kernel Hooks: Implements FreeRTOS diagnostic callbacks for
 *      heap allocation failure, stack overflow, idle, tick, and assertion hooks.
 *   5. Static Memory Buffers: Provides static memory allocations for the Idle Task
 *      and Timer Service Task (configUSE_STATIC_ALLOCATION = 1).
 * ============================================================================
 */

/* FreeRTOS Kernel API */
#include "FreeRTOS.h"
#include "task.h"

/* Standard C library includes */
#include <stdio.h>
#include <string.h>

/* Percepio TraceRecorder diagnostic interface */
#include <trcRecorder.h>

/*
 * Demo Selector Macro:
 * - 1 = Boots main_blinky() (Simulated POSIX Linux Environment Demo)
 * - 0 = Boots main_full() (Comprehensive FreeRTOS kernel regression test suite)
 */
#define mainCREATE_SIMPLE_BLINKY_DEMO_ONLY    1

/*
 * ============================================================================
 * ARM Cortex-M MPS2 AN385 UART0 Memory-Mapped I/O (MMIO) Registers
 * Base Address: 0x40004000UL
 * ============================================================================
 */
#define UART0_ADDRESS                         ( 0x40004000UL )
#define UART0_DATA                            ( *( ( ( volatile uint32_t * ) ( UART0_ADDRESS + 0UL ) ) ) )  /* Data Read/Write Register */
#define UART0_STATE                           ( *( ( ( volatile uint32_t * ) ( UART0_ADDRESS + 4UL ) ) ) )  /* Status Register (TX/RX full/empty) */
#define UART0_CTRL                            ( *( ( ( volatile uint32_t * ) ( UART0_ADDRESS + 8UL ) ) ) )  /* Control Register (TX/RX enable) */
#define UART0_BAUDDIV                         ( *( ( ( volatile uint32_t * ) ( UART0_ADDRESS + 16UL ) ) ) ) /* Baud Rate Divider */
#define TX_BUFFER_MASK                        ( 1UL )                                                       /* Bit 0: TX Buffer Full Flag */

/* External demo application function prototypes */
extern void main_blinky( void );
extern void main_full( void );

/* Comprehensive demo application hook declarations */
void vFullDemoTickHookFunction( void );
void vFullDemoIdleFunction( void );

/* Forward declaration of private hardware setup routine */
static void prvUARTInit( void );

/*---------------------------------------------------------------------------*/

/**
 * @brief Master system entry point invoked by Reset_Handler post-boot.
 *
 * Configures serial diagnostic hardware, initializes trace instrumentation,
 * and transfers control to the selected demo application (main_blinky).
 *
 * @return Returns 0 (conceptually), but execution transfers infinitely to the RTOS.
 */
int main( void )
{
    /*
     * 1. Initialize Percepio TraceRecorder (if enabled in FreeRTOSConfig.h):
     * Must be called before any FreeRTOS kernel API calls are made.
     */
#if (configUSE_TRACE_FACILITY == 1)
    xTraceInitialize();
    xTraceEnable(TRC_START);
    xTraceTimestampSetPeriod(configCPU_CLOCK_HZ / configTICK_RATE_HZ);
#endif

    /*
     * 2. Configure Hardware Peripherals:
     * Sets up UART0 registers to allow printf() output over serial terminal.
     */
    prvUARTInit();

    /*
     * 3. Launch Demo Application:
     * Dispatches execution to main_blinky() to boot the simulated Linux environment.
     */
    #if ( mainCREATE_SIMPLE_BLINKY_DEMO_ONLY == 1 )
    {
        main_blinky();
    }
    #else
    {
        main_full();
    }
    #endif

    return 0;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief FreeRTOS Heap Allocation Failure Hook.
 *
 * Triggered automatically by pvPortMalloc() if the heap memory pool
 * (configTOTAL_HEAP_SIZE) is exhausted. Disables interrupts and halts execution.
 */
void vApplicationMallocFailedHook( void )
{
    printf( "\r\n\r\n[CRITICAL] FreeRTOS Malloc Failed! Heap memory exhausted.\r\n" );
    portDISABLE_INTERRUPTS();

    /* Trap CPU in infinite loop for JTAG debugger attachment */
    for( ; ; )
    {
    }
}

/*---------------------------------------------------------------------------*/

/**
 * @brief FreeRTOS Idle Task Hook.
 *
 * Executed on every iteration of the background Idle task when no application
 * tasks are in the Ready state. Must never block or delay.
 */
void vApplicationIdleHook( void )
{
    /* In blinky mode, the idle task yields execution seamlessly */
}

/*---------------------------------------------------------------------------*/

/**
 * @brief FreeRTOS Task Stack Overflow Hook.
 *
 * Triggered if the kernel detects that a task's Process Stack Pointer (PSP)
 * has exceeded its allocated stack boundary (configCHECK_FOR_STACK_OVERFLOW).
 *
 * @param pxTask     Handle of the corrupted FreeRTOS task.
 * @param pcTaskName Null-terminated ASCII name string of the offending task.
 */
void vApplicationStackOverflowHook( TaskHandle_t pxTask,
                                    char * pcTaskName )
{
    ( void ) pcTaskName;
    ( void ) pxTask;

    printf( "\r\n\r\n[CRITICAL] Stack overflow detected in task: %s\r\n", pcTaskName );
    portDISABLE_INTERRUPTS();

    /* Trap CPU execution */
    for( ; ; )
    {
    }
}

/*---------------------------------------------------------------------------*/

/**
 * @brief FreeRTOS SysTick Timer Interrupt Hook.
 *
 * Called during every 1 ms SysTick timer interrupt tick (1000 Hz).
 * Executed in hardware interrupt context using Main Stack Pointer (MSP).
 */
void vApplicationTickHook( void )
{
    #if ( mainCREATE_SIMPLE_BLINKY_DEMO_ONLY != 1 )
    {
        vFullDemoTickHookFunction();
    }
    #endif
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Daemon / Timer Task Startup Hook.
 *
 * Invoked once when the FreeRTOS software timer daemon task begins execution.
 */
void vApplicationDaemonTaskStartupHook( void )
{
    xTraceEnable(TRC_START);
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Kernel Assertion Failure Callback.
 *
 * Triggered whenever a configASSERT(x) condition evaluates to false.
 * Prints file name and line number, disables interrupts, and halts.
 *
 * @param pcFileName Source code file path where assertion failed.
 * @param ulLine     Line number of the failed assertion.
 */
void vAssertCalled( const char * pcFileName,
                    uint32_t ulLine )
{
    volatile uint32_t ulSetToNonZeroInDebuggerToContinue = 0;

    printf( "[ASSERTION FAILURE] Line %d, file %s\r\n", ( int ) ulLine, pcFileName );

    taskENTER_CRITICAL();
    {
        /* Allows JTAG debugger to resume execution by setting variable to 1 */
        while( ulSetToNonZeroInDebuggerToContinue == 0 )
        {
            __asm volatile ( "NOP" );
            __asm volatile ( "NOP" );
        }
    }
    taskEXIT_CRITICAL();
}

/*---------------------------------------------------------------------------*/

/*
 * Static Memory Allocator for FreeRTOS Idle Task
 * Required when configUSE_STATIC_ALLOCATION == 1.
 */
void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxIdleTaskTCBBuffer,
                                    StackType_t ** ppxIdleTaskStackBuffer,
                                    uint32_t * pulIdleTaskStackSize )
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];

    *ppxIdleTaskTCBBuffer   = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

/*---------------------------------------------------------------------------*/

/*
 * Static Memory Allocator for FreeRTOS Timer Daemon Task
 * Required when configUSE_STATIC_ALLOCATION == 1 and configUSE_TIMERS == 1.
 */
void vApplicationGetTimerTaskMemory( StaticTask_t ** ppxTimerTaskTCBBuffer,
                                     StackType_t ** ppxTimerTaskStackBuffer,
                                     uint32_t * pulTimerTaskStackSize )
{
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ];

    *ppxTimerTaskTCBBuffer   = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Configures ARM Cortex-M MPS2 AN385 UART0 hardware registers.
 *
 * Sets the baud rate divider to 16 and enables transmitter control bits.
 */
static void prvUARTInit( void )
{
    UART0_BAUDDIV = 16; /* Configure default UART baud rate */
    UART0_CTRL    = 1;  /* Enable UART0 TX */
}

/*---------------------------------------------------------------------------*/

#ifdef __PICOLIBC__
/*
 * Picolibc standard I/O byte output stream wrapper
 */
int _uart_putc(char c, FILE *file)
{
    ( void ) file;

    /* Wait while TX buffer is full */
    while( ( UART0_STATE & TX_BUFFER_MASK ) != 0 )
    {
    }

    /* Write character to UART data register */
    UART0_DATA = c;
    return (unsigned char) c;
}

static FILE __stdio = FDEV_SETUP_STREAM(_uart_putc, NULL, NULL, _FDEV_SETUP_WRITE);
__attribute__( ( used ) ) FILE *const stdout = &__stdio;
#else
/*---------------------------------------------------------------------------*/

/**
 * @brief Low-level C library write hook for stdout redirection.
 *
 * Intercepts standard library printf() output buffers and streams each byte
 * directly to the MPS2 UART0 hardware data register (0x40004000UL).
 *
 * @param iFile         File descriptor index (1 for stdout, 2 for stderr).
 * @param pcString      Pointer to character buffer to transmit.
 * @param iStringLength Length in bytes of the character string.
 *
 * @return Number of characters successfully written.
 */
int __write( int iFile,
             char * pcString,
             int iStringLength )
{
    int iNextChar;
    ( void ) iFile;

    for( iNextChar = 0; iNextChar < iStringLength; iNextChar++ )
    {
        /* Wait until hardware TX FIFO has space available */
        while( ( UART0_STATE & TX_BUFFER_MASK ) != 0 )
        {
        }

        /* Emit byte to UART0 hardware register */
        UART0_DATA = *pcString;
        pcString++;
    }

    return iStringLength;
}

/*---------------------------------------------------------------------------*/

/**
 * @brief Guard against accidental standard C library malloc() invocations.
 *
 * FreeRTOS uses heap_4.c (pvPortMalloc) for deterministic memory allocation.
 * Calls to standard malloc() are intercepted and flagged as errors.
 */
void * malloc( size_t size )
{
    ( void ) size;

    printf( "\r\n\r\n[ERROR] Unexpected call to malloc() - use pvPortMalloc() instead!\r\n" );
    portDISABLE_INTERRUPTS();

    for( ; ; )
    {
    }
}
#endif
