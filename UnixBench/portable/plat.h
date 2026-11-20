/*******************************************************************************
 *  UnixBench portable harness - platform layer
 *
 *  Everything operating-system specific lives behind this interface, so the
 *  benchmark kernels and the runner are plain C89.  One plat_*.c file is
 *  linked per target:
 *
 *      plat_nt.c   Windows NT 3.51 / 4.0 and later (x86, PowerPC, MIPS,
 *                  Alpha, x64) - Win32 API only
 *      plat_dos.c  MS-DOS, 32-bit DPMI via DJGPP (386 and later)
 *      plat_mac.c  classic Mac OS, System 6 to 9, 68k and PowerPC (Retro68)
 *      plat_psx.c  POSIX: NetBSD/sparc (Makefile.sparc), and any host to
 *                  cross-check scores against the regular UnixBench
 *                  binaries
 *      plat_irix.c SGI IRIX 5.3 (o32) and IRIX 6.x (o32, n32), MIPS
 *      plat_hpux.c HP-UX 10.x/11.x, PA-RISC
 *
 *  The interface is shaped around what classic Mac OS can do:
 *
 *    - No preemptive threads and no signals.  The stop timer is a Time
 *      Manager task, which runs at interrupt time and may only store to
 *      memory.  So plat_alarm_start() only ever sets an int flag; it must
 *      never call stdio, malloc or exit.
 *    - No command line.  plat_init() may replace argc/argv (plat_mac.c
 *      asks for options on the console) before the runner parses them.
 *    - 68k compilers may use 16-bit int.  Counters are unsigned long and
 *      no interface here relies on int being wider than 16 bits.
 ******************************************************************************/

#ifndef UB_PLAT_H
#define UB_PLAT_H

/* Called first thing in main().  May rewrite the argument vector. */
void plat_init(int *argc, char ***argv);

/* Wall-clock time in seconds from an arbitrary origin, as precisely as the
   platform allows. */
double plat_seconds(void);

/* Arrange for *flag to become non-zero after the given number of seconds.
   Only one alarm is outstanding at a time. */
void plat_alarm_start(int seconds, volatile int *flag);

/* Release whatever plat_alarm_start() set up.  Called once the flag has
   been seen, or to abandon the alarm early. */
void plat_alarm_end(void);

/* One-line description of the OS, CPU and processor count. */
void plat_sysinfo(char *buf, int len);

/* Describe the CPU this binary's kernels were compiled for ("generic" for
   the default build).  Returns 0 if the running CPU lacks instructions the
   build needs: such a binary may die with an illegal instruction, or the
   OS may emulate the instructions and quietly wreck the timings. */
int plat_check_target(char *buf, int len);

#endif /* UB_PLAT_H */
