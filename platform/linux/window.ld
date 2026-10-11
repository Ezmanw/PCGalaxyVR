/* Added to the linker's own script (-T with INSERT): the game's window
   section (window.S) at 0xA0000000, after .bss, so all of the executable's
   code and data stay in its slot at 0x98000000 (port/port.h). */
SECTIONS
{
  .pcgvr_window 0xA0000000 (NOLOAD) : { KEEP(*(.pcgvr_window)) }
}
INSERT AFTER .bss;
