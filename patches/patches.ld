/*
 * Linker script for the port's C patches.
 *
 * Patch code and data live in the runtime's extended RDRAM from 0x80801000,
 * which is librecomp's patch_rdram_start: librecomp copies patches.bin there
 * before the game starts. They must end before 0x80F00000, where the port keeps
 * its private copy of the audio command list (src/callbacks.cpp), and well
 * before librecomp's mod region at 0x81000000.
 *
 * Game symbols are left undefined here (--unresolved-symbols=ignore-all) and
 * resolved by name when N64Recomp recompiles the patches against the game's
 * reference symbols. Host functions are given addresses in syms.ld.
 */

RAMBASE = 0x80801000;
PATCH_RAM_END = 0x80F00000;

MEMORY {
    extram : ORIGIN = RAMBASE, LENGTH = 7M
    rom : ORIGIN = 0, LENGTH = 7M
}

SECTIONS {
    .ctors         : { *(.ctors*) *(.init_array*)                } >extram AT >rom
    .dtors         : { *(.dtors*)                                } >extram AT >rom
    .text          : { *(.text*)                                 } >extram AT >rom
    .recomp_patch  : { *(.recomp_patch*) *(.recomp_force_patch*) } >extram AT >rom
    .recomp_export : { *(.recomp_export*)                        } >extram AT >rom
    .recomp_event  : { *(.recomp_event*)                         } >extram AT >rom
    .rodata        : { *(.rodata*)                               } >extram AT >rom
    .data          : { *(.data*)                                 } >extram AT >rom
    .bss  (NOLOAD) : { *(.bss*)  *(COMMON)                       } >extram
    ASSERT(. <= PATCH_RAM_END, "The patches have outgrown the RAM set aside for them")

    .reloc 0 : { *(.reloc*) }
    .symtab 0 : { *(.symtab) }
    .strtab 0 : { *(.strtab) }
    .shstrtab 0 : { *(.shstrtab) }

    /DISCARD/ : { *(*); }
}
