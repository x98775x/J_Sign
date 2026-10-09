/* Bootloader-safe memory map, based on BTT's gd32f20xxC_0x3000_iap.ld. */
ENTRY(Reset_Handler)
_Min_Heap_Size = 0x200;
_Min_Stack_Size = 0x400;
MEMORY {
 FLASH (rx) : ORIGIN = 0x08003000, LENGTH = 244K
 RAM (xrw)  : ORIGIN = 0x20000000, LENGTH = 128K
}
_estack = 0x20020000;
SECTIONS {
 .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)); . = ALIGN(4); } > FLASH
 .text : { . = ALIGN(4); *(.text) *(.text*) *(.glue_7) *(.glue_7t) *(.eh_frame) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); _etext = .; } > FLASH
 .rodata : { . = ALIGN(4); *(.rodata) *(.rodata*) . = ALIGN(4); } > FLASH
 .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
 .ARM : { __exidx_start = .; *(.ARM.exidx*); __exidx_end = .; } > FLASH
 .preinit_array : { PROVIDE_HIDDEN(__preinit_array_start = .); KEEP(*(.preinit_array*)); PROVIDE_HIDDEN(__preinit_array_end = .); } > FLASH
 .init_array : { PROVIDE_HIDDEN(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)); PROVIDE_HIDDEN(__init_array_end = .); } > FLASH
 .fini_array : { PROVIDE_HIDDEN(__fini_array_start = .); KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)); PROVIDE_HIDDEN(__fini_array_end = .); } > FLASH
 _sidata = LOADADDR(.data);
 .data : { . = ALIGN(4); _sdata = .; *(.data) *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
 .bss : { . = ALIGN(4); _sbss = .; __bss_start__ = .; *(.bss) *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; __bss_end__ = .; } > RAM
 ._user_heap_stack : { . = ALIGN(4); PROVIDE(end = .); PROVIDE(_end = .); . = . + _Min_Heap_Size + _Min_Stack_Size; . = ALIGN(4); } > RAM
 .ARM.attributes 0 : { *(.ARM.attributes) }
}
