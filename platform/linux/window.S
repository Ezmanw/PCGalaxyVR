/* The game's address window above the executable's own image: the low pool,
   the uncached mirrors of MEM1 and MEM2 and the locked cache, 0xA0000000 to
   0xE0010000 (port/port.h).  A zero-fill section of the executable, placed
   there by window.ld, so the kernel lays out the process (the brk heap
   included) around it; main.cpp turns it into a reservation. */
    .section .pcgvr_window,"aw",@nobits
    .globl pcgvr_window
    .type pcgvr_window, @object
pcgvr_window:
    .skip 0x40010000
    .size pcgvr_window, 0x40010000

    .section .note.GNU-stack,"",@progbits
