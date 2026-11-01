/*
 * Wi-Fi UART bridge, flash build: linked into the application area behind
 * the bootloader, which jumps to the reset vector at 0x08001000.
 */
ENTRY(bridge_reset)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08001000, LENGTH = 0x6FFC
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 8K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector : { KEEP(*(.isr_vector)) } >FLASH

  .text :
  {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  _sidata = LOADADDR(.data);
  .data :
  {
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT>FLASH

  .bss (NOLOAD) :
  {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } >RAM

  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}
