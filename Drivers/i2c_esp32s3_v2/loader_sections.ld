/* Firmware's native loader only maps the canonical .text executable output.
 * Keep the IDF I2C ISR's .iram1.* code and literal pools in that region.
 * Xtensa L32R can only reach backwards to its literal: ALL .literal.* and
 * .iram1.*.literal input sections MUST precede their .text/.iram1 code.
 * Otherwise the cross-link fails with "dangerous relocation: l32r: literal
 * placed after use". Catch any remaining orphans with the runtime-map audit.
 * INSERT preserves the default shared-ELF linker script and dynamic sections.
 */
SECTIONS {
  .text : {
    *(.literal .literal.* .gnu.linkonce.literal.*)
    *(.iram1.literal .iram1.*.literal)
    *(.text .text.* .gnu.linkonce.t.*)
    *(.iram1 .iram1.*)
  }
} INSERT BEFORE .rodata;
