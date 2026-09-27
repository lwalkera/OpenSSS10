/*
 * Linker script for the FT32F030C8AT7 application image.
 *
 * The stock bootloader occupies 0x08000000-0x08000FFF and jumps to the image
 * at 0x08001000. The image may be at most 0x6FFC bytes: the bootloader copies
 * 0x7000 bytes from the OTA staging area, whose last word (0x0800EFFC) is the
 * install marker. 0x0800F000-0x0800FFFF holds settings (see storage.c).
 *
 * Cortex-M0 has no VTOR, so startup copies the vector table to the first
 * 0xC0 bytes of SRAM and remaps SRAM to address 0.
 */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08001000, LENGTH = 0x6FFC
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 8K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

/* Minimum free RAM reserved for heap and stack; link fails if it doesn't fit */
_Min_Heap_Size  = 0;   /* no malloc */
_Min_Stack_Size = 0x400;

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    _svectors = .;
    KEEP(*(.isr_vector))
    . = ALIGN(4);
    _evectors = .;
    /* OTA image header at a fixed offset (0xC0) after the vector table */
    KEEP(*(.image_header))
  } >FLASH

  ASSERT(_evectors - _svectors == 0xC0, "vector table must be 48 words")

  /* Read-only data fills the space before the product key */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
    _erodata = .;
  } >FLASH

  ASSERT(_erodata <= ORIGIN(FLASH) + 0x1000, ".rodata overlaps the product key at 0x08002000")

  /*
   * Product key at image offset 0x1000. The stock firmware's OTA code only
   * installs an image whose 16 bytes here match its own key.
   */
  .product_key ORIGIN(FLASH) + 0x1000 :
  {
    KEEP(*(.product_key))
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)

    KEEP(*(.init))
    KEEP(*(.fini))

    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH
  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH
  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH

  /* Must be the first RAM section: SRAM is remapped to address 0 */
  .ram_vectors (NOLOAD) :
  {
    _sram_vectors = .;
    . = . + 0xC0;
  } >RAM

  ASSERT(_sram_vectors == ORIGIN(RAM), ".ram_vectors must start SRAM")

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  .bss (NOLOAD) :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  /* Heap grows up from here (newlib _sbrk uses 'end') */
  ._user_heap_stack (NOLOAD) :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  .ARM.attributes 0 : { *(.ARM.attributes) }

  _image_end = LOADADDR(.data) + SIZEOF(.data);
}
