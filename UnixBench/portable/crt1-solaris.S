/*******************************************************************************
 *  UnixBench portable harness - program start for Solaris/sparc (32-bit)
 *
 *  Solaris up to 9 has no crt1.o of its own: it came with the compiler.
 *  This is the one for Makefile.sparc's Solaris binaries, written from the
 *  SPARC ABI's description of the initial process stack:
 *
 *      %sp + 64    argc
 *      %sp + 68    argv[0] ... argv[argc - 1], NULL, envp[0] ..., NULL
 *      %g1         a function to register with atexit(), or 0
 *
 *  There are no constructors in the harness, so no _init/_fini (crti.o,
 *  crtn.o) either.  V7 instructions only.
 ******************************************************************************/

        .section ".text"
        .align  4
        .global _start
        .type   _start, #function
_start:
        mov     0, %fp                  ! end of the frame chain
        ld      [%sp + 64], %l0         ! argc
        add     %sp, 68, %l1            ! argv
        sll     %l0, 2, %l2
        add     %l2, 4, %l2
        add     %l1, %l2, %l2           ! envp = argv + argc + 1
        sethi   %hi(_environ), %l3
        st      %l2, [%l3 + %lo(_environ)]
        sub     %sp, 32, %sp            ! room for the callees' arguments

        orcc    %g1, %g0, %o0           ! the run-time linker's exit handler
        be      1f
         nop
        call    atexit
         nop
1:
        mov     %l0, %o0
        mov     %l1, %o1
        call    main
         mov    %l2, %o2
        call    exit
         nop
        call    _exit
         nop
        .size   _start, . - _start

/* libc.so.1 uses the program's environ if it has one. */
        .section ".data"
        .align  4
        .global _environ
        .weak   environ
        .type   _environ, #object
        .size   _environ, 4
_environ:
environ:
        .long   0
