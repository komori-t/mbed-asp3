MEMORY
{
    FLASH(rx) : ORIGIN = 0x00000000, LENGTH = 1M
    RAM(rwx) : ORIGIN =  0x20000000, LENGTH = 128k
}

ENTRY(_kernel_start)

PROVIDE(hardware_init_hook = 0);
PROVIDE(software_init_hook = 0);
PROVIDE(software_term_hook = 0);

SECTIONS
{
     /* MAIN TEXT SECTION */
    .text : ALIGN(4)
    {
        KEEP(*(.vector))
        *(.text*)
        *(.rodata .rodata.* .constdata .constdata.*)
        KEEP(*(.empty.*))
        . = ALIGN(4);
    } > FLASH

    __idata_start = .;
    /* Main DATA section (SRAM) */
    .data : ALIGN(4)
    {
        __data_start = .;
        *(.data*)
    } > RAM AT>FLASH
    __idata_end = __idata_start + SIZEOF(.data);

    /* MAIN BSS SECTION */
    .bss : ALIGN(4)
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM
}
