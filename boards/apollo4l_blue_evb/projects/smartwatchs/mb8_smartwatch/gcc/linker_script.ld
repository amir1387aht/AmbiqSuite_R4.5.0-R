/* Project-local layout. Keep GPU-visible allocations out of CPU TCM. */
ENTRY(Reset_Handler)
MEMORY {
    MCU_MRAM (rx) : ORIGIN = 0x00018000, LENGTH = 1998848
    MCU_TCM (rwx) : ORIGIN = 0x10000000, LENGTH = 384K
    SHARED_SRAM (rwx) : ORIGIN = 0x10060000, LENGTH = 1024K
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
        . = ALIGN(32); __display_buffers_start__ = .;
        *(.display_buffer*) . = ALIGN(32); __display_buffers_end__ = .;
    } > SHARED_SRAM
    __shared_static_end__ = .;
    /* All RAM after static data belongs to the application, not to a
     * graphics-private reservation. Includes TLSF's control/block overhead. */
    .app_cpu_heap (NOLOAD) : {
        . = ALIGN(8); __app_cpu_heap_start__ = .;
        . = ORIGIN(MCU_TCM) + LENGTH(MCU_TCM); __app_cpu_heap_end__ = .;
    } > MCU_TCM
    .app_shared_heap (NOLOAD) : {
        . = ALIGN(32); __app_shared_heap_start__ = .;
        . = ORIGIN(SHARED_SRAM) + LENGTH(SHARED_SRAM); __app_shared_heap_end__ = .;
    } > SHARED_SRAM
    .ARM.attributes 0 : { *(.ARM.attributes) }
}
__cpu_ram_start__ = ORIGIN(MCU_TCM);
__cpu_ram_end__ = ORIGIN(MCU_TCM) + LENGTH(MCU_TCM);
__shared_ram_start__ = ORIGIN(SHARED_SRAM);
__shared_ram_end__ = ORIGIN(SHARED_SRAM) + LENGTH(SHARED_SRAM);
ASSERT(__shared_static_end__ <= __shared_ram_end__, "Static shared SRAM overflow")
ASSERT(_edata <= __cpu_ram_end__, "CPU TCM overflow")

ASSERT(_edata <= __app_cpu_heap_start__, "CPU application heap overlaps data")
ASSERT(__shared_static_end__ <= __app_shared_heap_start__, "Shared application heap overlaps static data")
ASSERT(__app_cpu_heap_end__ == 0x10060000, "CPU TCM end mismatch")
ASSERT(__app_shared_heap_end__ == 0x10160000, "Shared SRAM end mismatch")
