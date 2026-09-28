/*
 * Wi-Fi UART bridge: runs entirely from SRAM, loaded over SWD.
 * Load address = run address, so OpenOCD's load_image puts every section in
 * place; the entry code only has to zero .bss.
 */
ENTRY(bridge_entry)

MEMORY
{
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 8K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .text :
  {
    KEEP(*(.entry))      /* must be first: `resume 0x20000000` jumps here */
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } >RAM

  ASSERT(bridge_entry == ORIGIN(RAM), "bridge_entry must be at the start of SRAM")

  .data : { *(.data*) . = ALIGN(4); } >RAM

  .bss (NOLOAD) :
  {
    _sbss = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } >RAM

  /* 0x20001000-0x200017FF is OpenOCD's work area while loading (Makefile) */
  ASSERT(_ebss <= 0x20001000, "bridge overlaps the OpenOCD work area")

  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}
