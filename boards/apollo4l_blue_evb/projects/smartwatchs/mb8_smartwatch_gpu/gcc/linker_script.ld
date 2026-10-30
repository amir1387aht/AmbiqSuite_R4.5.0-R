/* Project-local layout. Keep GPU-visible allocations out of CPU TCM. */
ENTRY(Reset_Handler)
MEMORY {
    MCU_MRAM (rx) : ORIGIN = 0x00018000, LENGTH = 1998848
    MCU_TCM (rwx) : ORIGIN = 0x10000000, LENGTH = 256K
    LVGL_TCM (rw) : ORIGIN = 0x10040000, LENGTH = 128K
    SHARED_SRAM (rwx) : ORIGIN = 0x10060000, LENGTH = 512K
    LVGL_SSRAM (rw) : ORIGIN = 0x100e0000, LENGTH = 512K
}
SECTIONS {
    .text : {
        . = ALIGN(4);
        KEEP(*(.isr_vector)) KEEP(*(.patch))
        *(.text*) *(.rodata*)
        . = ALIGN(4); _etext = .;
    } > MCU_MRAM
    .ARM.extab : { *(.ARM.extab*) } > MCU_MRAM
    .ARM.exidx : {
        __exidx_start = .; *(.ARM.exidx*) __exidx_end = .;
    } > MCU_MRAM
    .data : {
        . = ALIGN(8); _sdata = .;
        *(.data*) *(.ramfunc*)
        . = ALIGN(8); _edata = .;
    } > MCU_TCM AT> MCU_MRAM
    _init_data = LOADADDR(.data);
    .stack (NOLOAD) : {
        . = ALIGN(8); __stack_start__ = .;
        KEEP(*(.stack*))
        . = ALIGN(8); __stack_end__ = .;
    } > SHARED_SRAM
    .heap (NOLOAD) : {
        . = ALIGN(8); __heap_start__ = .;
        KEEP(*(.heap*))
        . = __heap_start__ + 16K;
        __heap_end__ = .;
    } > SHARED_SRAM
    .bss (NOLOAD) : {
        . = ALIGN(8); _sbss = .;
        *(.bss*) *(COMMON) KEEP(*(.shared*))
        . = ALIGN(8); _ebss = .;
    } > SHARED_SRAM
    .shared (NOLOAD) : {
        . = ALIGN(8); KEEP(*(.resource_table))
        . = ALIGN(8);
    } > SHARED_SRAM
    .buffers (NOLOAD) : {
        . = ALIGN(32); *(.display_buffer*) . = ALIGN(32);
    } > SHARED_SRAM
    .lvgl_tcm (NOLOAD) : {
        __lvgl_tcm_start__ = .; . += LENGTH(LVGL_TCM); __lvgl_tcm_end__ = .;
    } > LVGL_TCM
    .lvgl_ssram (NOLOAD) : {
        __lvgl_ssram_start__ = .; . += LENGTH(LVGL_SSRAM); __lvgl_ssram_end__ = .;
    } > LVGL_SSRAM
    .ARM.attributes 0 : { *(.ARM.attributes) }
}
ASSERT(__lvgl_tcm_start__ == 0x10040000, "LVGL TCM config mismatch")
ASSERT(__lvgl_ssram_start__ == 0x100e0000, "LVGL SSRAM config mismatch")
ASSERT(__heap_end__ <= _sbss, "C heap overlaps BSS")
