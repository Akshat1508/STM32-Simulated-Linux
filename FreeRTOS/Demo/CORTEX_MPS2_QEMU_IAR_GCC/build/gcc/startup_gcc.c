/*
 * ============================================================================
 * File: startup_gcc.c
 * Description: ARM Cortex-M3 Vector Table, System Reset Handler & Fault Diagnostics
 * Target Platform: ARM Cortex-M3 (MPS2 AN385 Platform on QEMU)
 *
 * This file configures the foundational hardware startup code for GCC:
 *   1. Hardware Interrupt Vector Table (isr_vector[]): Placed into the
 *      .isr_vector section at Flash base address 0x00000000.
 *   2. Core FreeRTOS Exception Handlers: Binds vPortSVCHandler (SVC),
 *      xPortPendSVHandler (PendSV context switch), and xPortSysTickHandler.
 *   3. SMSC9118 Ethernet Driver Binding: Maps NVIC IRQ 13 to EthernetISR.
 *   4. HardFault Diagnostic Trap: Assembly handler extracting CPU registers
 *      (R0-R3, R12, LR, PC, xPSR) from the active stack (MSP or PSP).
 * ============================================================================
 */

#include <stdint.h>
#include <stdio.h>

/* ============================================================================
 * External FreeRTOS Kernel Handlers & Application ISRs
 * ============================================================================
 */
extern void vPortSVCHandler( void );     /* FreeRTOS SVC Handler (starts first task) */
extern void xPortPendSVHandler( void );   /* FreeRTOS PendSV Handler (context switch) */
extern void xPortSysTickHandler( void );  /* FreeRTOS SysTick 1 ms timer handler */
extern void TIMER0_Handler( void );       /* Hardware Timer 0 ISR */
extern void TIMER1_Handler( void );       /* Hardware Timer 1 ISR */
extern void EthernetISR( void );          /* SMSC9118 LAN9118 Ethernet IRQ 13 ISR */

/* Core exception and reset function prototypes */
static void HardFault_Handler( void ) __attribute__( ( naked ) );
static void Default_Handler( void ) __attribute__( ( naked ) );
void Reset_Handler( void ) __attribute__( ( naked ) );

/* Application entry point and linker script symbols */
extern int main( void );
extern uint32_t _estack; /* Top of stack defined in mps2_m3.ld (end of SRAM) */

/*
 * ============================================================================
 * ARM Cortex-M3 Interrupt Vector Table
 * Section: .isr_vector (Mapped by linker to Flash origin 0x00000000)
 * ============================================================================
 */
const uint32_t* isr_vector[] __attribute__((section(".isr_vector"), used)) =
{
    ( uint32_t * ) &_estack,            /* Initial Main Stack Pointer (MSP) */
    ( uint32_t * ) &Reset_Handler,      /* Reset Handler                 (-15) */
    ( uint32_t * ) &Default_Handler,    /* Non-Maskable Interrupt (NMI)  (-14) */
    ( uint32_t * ) &HardFault_Handler,  /* HardFault Handler             (-13) */
    ( uint32_t * ) &Default_Handler,    /* MemManage Fault Handler       (-12) */
    ( uint32_t * ) &Default_Handler,    /* BusFault Handler              (-11) */
    ( uint32_t * ) &Default_Handler,    /* UsageFault Handler            (-10) */
    0,                                  /* Reserved                       (-9) */
    0,                                  /* Reserved                       (-8) */
    0,                                  /* Reserved                       (-7) */
    0,                                  /* Reserved                       (-6) */
    ( uint32_t * ) &vPortSVCHandler,     /* FreeRTOS Supervisor Call (SVC) (-5) */
    ( uint32_t * ) &Default_Handler,    /* Debug Monitor Handler          (-4) */
    0,                                  /* Reserved                       (-3) */
    ( uint32_t * ) &xPortPendSVHandler,  /* FreeRTOS PendSV Context Switch (-2) */
    ( uint32_t * ) &xPortSysTickHandler, /* FreeRTOS SysTick 1ms Timer     (-1) */
    0,                                  /* External IRQ 0                      */
    0,                                  /* External IRQ 1                      */
    0,                                  /* External IRQ 2                      */
    0,                                  /* External IRQ 3                      */
    0,                                  /* External IRQ 4                      */
    0,                                  /* External IRQ 5                      */
    0,                                  /* External IRQ 6                      */
    0,                                  /* External IRQ 7                      */
    ( uint32_t * ) TIMER0_Handler,      /* Hardware Timer 0 (IRQ 8)            */
    ( uint32_t * ) TIMER1_Handler,      /* Hardware Timer 1 (IRQ 9)            */
    0,                                  /* External IRQ 10                     */
    0,                                  /* External IRQ 11                     */
    0,                                  /* External IRQ 12                     */
    ( uint32_t * ) EthernetISR,         /* SMSC9118 Ethernet Controller (IRQ 13)*/
};

/*
 * ----------------------------------------------------------------------------
 * Reset_Handler — System Boot Entry Point
 * Executed immediately upon processor power-on or hardware reset.
 * ----------------------------------------------------------------------------
 */
void Reset_Handler( void )
{
    /* Transfer control to main() in main.c */
    (void) main();
}

/* ============================================================================
 * HardFault Diagnostic State Storage
 * Stores register values captured at the time of a CPU fault.
 * ============================================================================
 */
volatile uint32_t r0;
volatile uint32_t r1;
volatile uint32_t r2;
volatile uint32_t r3;
volatile uint32_t r12;
volatile uint32_t lr;  /* Link Register (Return Address) */
volatile uint32_t pc;  /* Program Counter (Faulting Instruction Address) */
volatile uint32_t psr; /* Program Status Register */

/**
 * @brief Diagnostic helper extracting CPU registers from the stacked frame.
 *
 * @param pulFaultStackAddress Base address of the stacked exception frame.
 */
static __attribute__( ( used ) ) void prvGetRegistersFromStack( uint32_t *pulFaultStackAddress )
{
    r0  = pulFaultStackAddress[ 0 ];
    r1  = pulFaultStackAddress[ 1 ];
    r2  = pulFaultStackAddress[ 2 ];
    r3  = pulFaultStackAddress[ 3 ];
    r12 = pulFaultStackAddress[ 4 ];
    lr  = pulFaultStackAddress[ 5 ];
    pc  = pulFaultStackAddress[ 6 ];
    psr = pulFaultStackAddress[ 7 ];

    printf( "\r\n[HARDFAULT] PC: 0x%08X, LR: 0x%08X, PSR: 0x%08X\r\n",
            (unsigned int)pc, (unsigned int)lr, (unsigned int)psr );
    fflush( stdout );

    /* Lock CPU in loop for JTAG debugger inspection */
    for( ;; );
}

/**
 * @brief Default Handler for unhandled peripheral interrupts.
 *
 * Queries the ICSR register (0xE000ED04) to identify the executing interrupt number
 * and halts in an infinite loop.
 */
void Default_Handler( void )
{
    __asm volatile
    (
        ".align 8                                \n"
        " ldr r3, =0xe000ed04                    \n" /* Load ICSR address */
        " ldr r2, [r3, #0]                       \n" /* Read ICSR register value */
        " uxtb r2, r2                            \n" /* Extract active interrupt number (bits 0-7) */
        "Infinite_Loop:                          \n"
        " b  Infinite_Loop                       \n" /* Trap execution */
        " .ltorg                                 \n"
    );
}

/**
 * @brief Assembly HardFault Handler.
 *
 * Inspects bit 2 of the Link Register (EXC_RETURN) to determine whether the fault
 * occurred on the Main Stack (MSP) or Process Stack (PSP), loads the stack pointer
 * into R0, and branches to prvGetRegistersFromStack().
 */
void HardFault_Handler( void )
{
    __asm volatile
    (
        ".align 8                                                   \n"
        " tst lr, #4                                                \n" /* Test EXC_RETURN bit 2 (0=MSP, 1=PSP) */
        " ite eq                                                    \n"
        " mrseq r0, msp                                             \n" /* Fault on MSP: pass MSP to R0 */
        " mrsne r0, psp                                             \n" /* Fault on PSP: pass PSP to R0 */
        " ldr r1, [r0, #24]                                         \n" /* Read stacked PC address */
        " ldr r2, =prvGetRegistersFromStack                         \n"
        " bx r2                                                     \n" /* Jump to C fault reporter */
        " .ltorg                                                    \n"
    );
}
