/*******************************************************************************
 *  UnixBench portable harness - MS-DOS platform layer (DJGPP)
 *
 *  A 32-bit protected-mode DOS program built with DJGPP.  It needs a DPMI
 *  host: build-dos.sh binds CWSDPMI into the .exe, so it runs from plain DOS
 *  and also uses the host's DPMI under Windows 3.x/9x or an NT DOS box.
 *
 *  The alarm chains a protected-mode handler onto IRQ 0 (the 18.2 Hz BIOS
 *  timer) that counts ticks down and then stores to the flag.  DJGPP's own
 *  setitimer()/SIGALRM never fired under DOSBox-X, so it is not used.  The
 *  handler is removed at exit and on Ctrl-C/Ctrl-Break.
 *
 *  The clock is uclock(): the BIOS tick count plus the 8254 PIT counter,
 *  0.84 us resolution.  Some DOS boxes emulate the PIT badly, so plat_init()
 *  checks uclock() against the BIOS tick and falls back to the tick (55 ms,
 *  about 0.5% of a 10-second pass) if they disagree.
 ******************************************************************************/

#include <cpuid.h>
#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "plat.h"

/* Which CPU the kernels were compiled for; Makefile.dos passes
   -DUB_TARGET_<variant>.  Level is the x86 family (3 = 386 ... 6 = P6). */
#if defined(UB_TARGET_486)
#define UB_TARGET_NAME  "i486"
#define UB_TARGET_LEVEL 4
#define UB_TARGET_CMOV  0
#elif defined(UB_TARGET_586)
#define UB_TARGET_NAME  "Pentium (i586)"
#define UB_TARGET_LEVEL 5
#define UB_TARGET_CMOV  0
#elif defined(UB_TARGET_686)
#define UB_TARGET_NAME  "Pentium Pro (i686, CMOV)"
#define UB_TARGET_LEVEL 6
#define UB_TARGET_CMOV  1
#else
#define UB_TARGET_NAME  "generic (i386)"
#define UB_TARGET_LEVEL 0
#define UB_TARGET_CMOV  0
#endif

#define BIOS_TICKS_PER_SEC  18.2064819336   /* 1193180 / 65536 */
#define BIOS_TICKS_PER_DAY  0x1800B0L

static volatile int *plat_alarm_flag;
static int plat_use_uclock;
static long plat_tick_base;     /* BIOS tick at plat_init(), for midnight */
static long plat_tick_days;
static void plat_unhook(void);

static void plat_on_break(int sig)
{
    plat_unhook();
    signal(sig, SIG_DFL);
    raise(sig);
}

/* ---------------------------------------------------------------- clock */

static long plat_bios_ticks(void)
{
    long t = (long)rawclock();

    /* rawclock() resets to 0 at midnight; keep counting. */
    if (t < plat_tick_base)
        plat_tick_days++;
    plat_tick_base = t;
    return t + plat_tick_days * BIOS_TICKS_PER_DAY;
}

static long plat_wait_tick(void)
{
    long t0 = plat_bios_ticks(), t;

    while ((t = plat_bios_ticks()) == t0)
        ;
    return t;
}

void plat_init(int *argc, char ***argv)
{
    long t0, t1;
    uclock_t u0, u1;
    double ratio;

    (void)argc;
    (void)argv;
    plat_tick_base = (long)rawclock();
    atexit(plat_unhook);
    signal(SIGINT, plat_on_break);
    signal(SIGQUIT, plat_on_break);

    /* Time 5 BIOS ticks (about 275 ms) with both clocks. */
    (void)uclock();
    t0 = plat_wait_tick();
    u0 = uclock();
    do
        t1 = plat_bios_ticks();
    while (t1 - t0 < 5);
    u1 = uclock();
    ratio = ((double)(u1 - u0) / UCLOCKS_PER_SEC)
          / ((double)(t1 - t0) / BIOS_TICKS_PER_SEC);
    plat_use_uclock = ratio > 0.97 && ratio < 1.03;
}

double plat_seconds(void)
{
    if (plat_use_uclock)
        return (double)uclock() / UCLOCKS_PER_SEC;
    return (double)plat_bios_ticks() / BIOS_TICKS_PER_SEC;
}

/* ---------------------------------------------------------------- alarm */

/* The handler runs at interrupt time: it may only touch these (locked)
   variables.  plat_alarm_flag points at ub_stop, which is locked too. */
static volatile long plat_ticks_left;
static _go32_dpmi_seginfo plat_old_irq0, plat_new_irq0;
static int plat_irq0_hooked;

static void plat_irq0(void)
{
    if (plat_ticks_left > 0 && --plat_ticks_left == 0)
        *plat_alarm_flag = 1;
}
static void plat_irq0_end(void) { }

void plat_alarm_start(int seconds, volatile int *flag)
{
    plat_alarm_flag = flag;
    /* +1: the first tick may come at once. */
    plat_ticks_left = (long)(seconds * BIOS_TICKS_PER_SEC + 0.5) + 1;

    if (!plat_irq0_hooked) {
        _go32_dpmi_lock_code((void *)plat_irq0,
                             (char *)plat_irq0_end - (char *)plat_irq0);
        _go32_dpmi_lock_data((void *)&plat_ticks_left, sizeof(plat_ticks_left));
        _go32_dpmi_lock_data((void *)&plat_alarm_flag, sizeof(plat_alarm_flag));
        _go32_dpmi_lock_data((void *)flag, sizeof(*flag));
        _go32_dpmi_get_protected_mode_interrupt_vector(8, &plat_old_irq0);
        plat_new_irq0.pm_offset = (unsigned long)plat_irq0;
        plat_new_irq0.pm_selector = _go32_my_cs();
        if (_go32_dpmi_chain_protected_mode_interrupt_vector(8, &plat_new_irq0)
            != 0) {
            fprintf(stderr, "cannot hook the timer interrupt\n");
            *flag = 1;
            return;
        }
        plat_irq0_hooked = 1;
    }
}

void plat_alarm_end(void)
{
    /* Stay hooked across passes; disarm only.  Unhooked at exit. */
    plat_ticks_left = 0;
}

static void plat_unhook(void)
{
    if (plat_irq0_hooked) {
        _go32_dpmi_set_protected_mode_interrupt_vector(8, &plat_old_irq0);
        plat_irq0_hooked = 0;
    }
}

/* ---------------------------------------------------------------- CPU */

/* Toggle an EFLAGS bit; non-zero if it sticks.  AC (bit 18) is missing on
   the 386; ID (bit 21) means CPUID is available. */
static int plat_eflags_bit(unsigned long bit)
{
    unsigned long before, after;

    __asm__ __volatile__(
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %1\n\t"
        "xorl %2, %1\n\t"
        "pushl %1\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %1\n\t"
        "pushl %0\n\t"
        "popfl"
        : "=&r"(before), "=&r"(after)
        : "ri"(bit)
        : "cc");
    return ((before ^ after) & bit) != 0;
}

typedef struct {
    int family;             /* 3, 4, 5, 6, ... */
    int model, stepping;
    int has_cpuid;
    int has_cmov;
    char vendor[13];
    char brand[49];
} plat_cpu;

static void plat_get_cpu(plat_cpu *c)
{
    unsigned int a, b, d, x;
    unsigned int r[12];
    char *p;

    memset(c, 0, sizeof(*c));
    if (!plat_eflags_bit(1UL << 18)) {
        c->family = 3;
        return;
    }
    c->family = 4;
    if (!plat_eflags_bit(1UL << 21))
        return;
    c->has_cpuid = 1;

    __cpuid(0, a, b, x, d);
    memcpy(c->vendor + 0, &b, 4);
    memcpy(c->vendor + 4, &d, 4);
    memcpy(c->vendor + 8, &x, 4);
    c->vendor[12] = '\0';
    if (a >= 1) {
        __cpuid(1, a, b, x, d);
        c->family = (a >> 8) & 0xF;
        c->model = (a >> 4) & 0xF;
        c->stepping = a & 0xF;
        if (c->family == 0xF)
            c->family += (a >> 20) & 0xFF;
        if (c->family >= 6)
            c->model += ((a >> 16) & 0xF) << 4;
        c->has_cmov = (d >> 15) & 1;
    }
    __cpuid(0x80000000U, a, b, x, d);
    if (a >= 0x80000004U && a < 0x8000FFFFU) {
        __cpuid(0x80000002U, r[0], r[1], r[2], r[3]);
        __cpuid(0x80000003U, r[4], r[5], r[6], r[7]);
        __cpuid(0x80000004U, r[8], r[9], r[10], r[11]);
        memcpy(c->brand, r, 48);
        c->brand[48] = '\0';
        for (p = c->brand; *p == ' '; p++)
            ;
        memmove(c->brand, p, strlen(p) + 1);
    }
}

static void plat_cpu_str(const plat_cpu *c, char *buf)
{
    if (c->brand[0])
        sprintf(buf, "%s", c->brand);
    else if (c->has_cpuid)
        sprintf(buf, "%s family %d model %d stepping %d", c->vendor,
                c->family, c->model, c->stepping);
    else
        sprintf(buf, "%s (no CPUID)", c->family == 3 ? "386" : "486");
}

int plat_check_target(char *buf, int len)
{
    plat_cpu c;
    char cpu[64], tmp[160];
    int ok = 1;

    strncpy(buf, UB_TARGET_NAME, len - 1);
    buf[len - 1] = '\0';
    if (UB_TARGET_LEVEL == 0)
        return 1;

    plat_get_cpu(&c);
    if (c.family < UB_TARGET_LEVEL)
        ok = 0;
    if (UB_TARGET_CMOV && !c.has_cmov)
        ok = 0;
    if (!ok) {
        plat_cpu_str(&c, cpu);
        sprintf(tmp, "%s - this %s does not support it", UB_TARGET_NAME, cpu);
        strncpy(buf, tmp, len - 1);
        buf[len - 1] = '\0';
    }
    return ok;
}

/* ---------------------------------------------------------------- system */

/* Name of the environment the DOS program is running in. */
static const char *plat_host(void)
{
    __dpmi_regs r;
    unsigned short tv = _get_dos_version(1);

    if (tv == 0x0532)
        return "Windows NT DOS box";
    memset(&r, 0, sizeof(r));
    r.x.ax = 0x1600;            /* Windows enhanced-mode check */
    __dpmi_int(0x2F, &r);
    if (r.h.al >= 4 && r.h.al != 0x80)
        return "Windows 9x DOS box";
    if (r.h.al != 0 && r.h.al != 0x80)
        return "Windows 3.x DOS box";
    return NULL;
}

void plat_sysinfo(char *buf, int len)
{
    plat_cpu c;
    char cpu[64], tmp[256];
    unsigned short tv = _get_dos_version(1);
    const char *host = plat_host();

    plat_get_cpu(&c);
    plat_cpu_str(&c, cpu);
    sprintf(tmp, "%s %d.%02d%s%s; %s; FPU %s; clock %s",
            _os_flavor, tv >> 8, tv & 0xFF,
            host ? ", " : "", host ? host : "",
            cpu, _8087 ? "present" : "emulated",
            plat_use_uclock ? "PIT (uclock)" : "BIOS tick (55 ms)");
    strncpy(buf, tmp, len - 1);
    buf[len - 1] = '\0';
}
