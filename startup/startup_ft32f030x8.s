/**
 * GCC startup for FT32F030x8 (Cortex-M0).
 *
 * Ported from the pack's MDK-ARM startup_ft32f030x8.s: same vector table and
 * the same "booted from system memory" remap check in Reset_Handler.
 */

    .syntax unified
    .cpu cortex-m0
    .fpu softvfp
    .thumb

    .global g_pfnVectors
    .global Default_Handler

    .section .text.Reset_Handler
    .weak Reset_Handler
    .type Reset_Handler, %function
Reset_Handler:
    cpsid i                    /* main() enables interrupts after init */
    ldr   r0, =_estack
    mov   sp, r0

    /*
     * The image runs at 0x08001000 behind the stock bootloader. Cortex-M0 has
     * no VTOR, so copy the vector table to the start of SRAM and remap SRAM
     * to address 0.
     */
    ldr   r0, =_sram_vectors
    ldr   r1, =_svectors
    ldr   r2, =_evectors
6:  ldr   r3, [r1]
    str   r3, [r0]
    adds  r0, r0, #4
    adds  r1, r1, #4
    cmp   r1, r2
    bcc   6b

    ldr   r0, =0x40021018      /* RCC_APB2ENR: enable SYSCFG clock */
    ldr   r1, [r0]
    movs  r2, #1
    orrs  r1, r1, r2
    str   r1, [r0]
    ldr   r0, =0x40010000      /* SYSCFG_CFGR1: MEM_MODE = 3 (SRAM at 0) */
    ldr   r1, [r0]
    movs  r2, #3
    orrs  r1, r1, r2
    str   r1, [r0]

    /* Copy .data from flash to RAM */
    ldr   r0, =_sdata
    ldr   r1, =_edata
    ldr   r2, =_sidata
    b     2f
1:  ldr   r3, [r2]
    str   r3, [r0]
    adds  r0, r0, #4
    adds  r2, r2, #4
2:  cmp   r0, r1
    bcc   1b

    /* Zero .bss */
    ldr   r0, =_sbss
    ldr   r1, =_ebss
    movs  r2, #0
    b     4f
3:  str   r2, [r0]
    adds  r0, r0, #4
4:  cmp   r0, r1
    bcc   3b

    bl    SystemInit
    bl    __libc_init_array
    bl    main
5:  b     5b
    .size Reset_Handler, .-Reset_Handler

/* Built with -nostartfiles, so crti.o's _init is absent; __libc_init_array calls it */
    .section .text._init, "ax", %progbits
    .weak _init
    .type _init, %function
_init:
    bx    lr
    .size _init, .-_init

    .section .text.Default_Handler, "ax", %progbits
Default_Handler:
Infinite_Loop:
    b     Infinite_Loop
    .size Default_Handler, .-Default_Handler

    .section .isr_vector, "a", %progbits
    .type g_pfnVectors, %object
g_pfnVectors:
    .word _estack
    .word Reset_Handler
    .word NMI_Handler
    .word HardFault_Handler
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word SVC_Handler
    .word 0
    .word 0
    .word PendSV_Handler
    .word SysTick_Handler

    /* External interrupts */
    .word WWDG_IRQHandler                /* Window Watchdog */
    .word PVD_VDDIO_IRQHandler           /* PVD_VDDIO */
    .word RTC_IRQHandler                 /* RTC through EXTI line */
    .word FLASH_IRQHandler               /* FLASH */
    .word RCC_IRQHandler                 /* RCC */
    .word EXTI0_1_IRQHandler             /* EXTI line 0 and 1 */
    .word EXTI2_3_IRQHandler             /* EXTI line 2 and 3 */
    .word EXTI4_15_IRQHandler            /* EXTI line 4 to 15 */
    .word 0
    .word DMA1_Channel1_IRQHandler       /* DMA1 channel 1 */
    .word DMA1_Channel2_3_IRQHandler     /* DMA1 channel 2 and 3 */
    .word DMA1_Channel4_5_IRQHandler     /* DMA1 channel 4 and 5 */
    .word ADC1_IRQHandler                /* ADC1 */
    .word TIM1_BRK_UP_TRG_COM_IRQHandler /* TIM1 break, update, trigger, commutation */
    .word TIM1_CC_IRQHandler             /* TIM1 capture compare */
    .word 0
    .word TIM3_IRQHandler                /* TIM3 */
    .word TIM6_IRQHandler                /* TIM6 */
    .word 0
    .word TIM14_IRQHandler               /* TIM14 */
    .word TIM15_IRQHandler               /* TIM15 */
    .word TIM16_IRQHandler               /* TIM16 */
    .word TIM17_IRQHandler               /* TIM17 */
    .word I2C1_IRQHandler                /* I2C1 */
    .word I2C2_IRQHandler                /* I2C2 */
    .word SPI1_IRQHandler                /* SPI1 */
    .word SPI2_IRQHandler                /* SPI2 */
    .word USART1_IRQHandler              /* USART1 */
    .word USART2_IRQHandler              /* USART2 */
    .word 0
    .word 0
    .word USB_IRQHandler                 /* USB */
    .size g_pfnVectors, .-g_pfnVectors

/* Every handler is a weak alias to Default_Handler; define one in C to override. */
    .macro weak_handler name
    .weak \name
    .thumb_set \name, Default_Handler
    .endm

    weak_handler NMI_Handler
    weak_handler HardFault_Handler
    weak_handler SVC_Handler
    weak_handler PendSV_Handler
    weak_handler SysTick_Handler
    weak_handler WWDG_IRQHandler
    weak_handler PVD_VDDIO_IRQHandler
    weak_handler RTC_IRQHandler
    weak_handler FLASH_IRQHandler
    weak_handler RCC_IRQHandler
    weak_handler EXTI0_1_IRQHandler
    weak_handler EXTI2_3_IRQHandler
    weak_handler EXTI4_15_IRQHandler
    weak_handler DMA1_Channel1_IRQHandler
    weak_handler DMA1_Channel2_3_IRQHandler
    weak_handler DMA1_Channel4_5_IRQHandler
    weak_handler ADC1_IRQHandler
    weak_handler TIM1_BRK_UP_TRG_COM_IRQHandler
    weak_handler TIM1_CC_IRQHandler
    weak_handler TIM3_IRQHandler
    weak_handler TIM6_IRQHandler
    weak_handler TIM14_IRQHandler
    weak_handler TIM15_IRQHandler
    weak_handler TIM16_IRQHandler
    weak_handler TIM17_IRQHandler
    weak_handler I2C1_IRQHandler
    weak_handler I2C2_IRQHandler
    weak_handler SPI1_IRQHandler
    weak_handler SPI2_IRQHandler
    weak_handler USART1_IRQHandler
    weak_handler USART2_IRQHandler
    weak_handler USB_IRQHandler
