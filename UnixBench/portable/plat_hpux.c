/*******************************************************************************
 *  UnixBench portable harness - HP-UX platform layer
 *
 *  Written for HP-UX 10.20 on PA-RISC (HP ANSI C or gcc) and should build
 *  on HP-UX 10.x and 11.x.  Uses gettimeofday(), alarm(), uname(),
 *  sysconf(_SC_CPU_VERSION) and pstat.
 ******************************************************************************/

#ifndef _HPUX_SOURCE
#define _HPUX_SOURCE            /* cc -Ae sets it; gcc -ansi does not */
#endif

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/param.h>
#include <sys/pstat.h>
#include "plat.h"

/* -DUB_TARGET_<cpu> records the processor the kernels were scheduled for
   (cc +DS<cpu>, gcc -mschedule=).  All builds are PA-RISC 1.1 code. */
#if defined(UB_TARGET_PA7100LC)
#define UB_TARGET_NAME  "PA-RISC 1.1, scheduled for the PA-7100LC"
#elif defined(UB_TARGET_PA7000)
#define UB_TARGET_NAME  "PA-RISC 1.1 generic (scheduled for the PA-7000)"
#else
#define UB_TARGET_NAME  "PA-RISC 1.1"
#endif

#if defined(__GNUC__)
#define UB_COMPILER     ", gcc " __VERSION__
#else
#define UB_COMPILER     ", HP C"
#endif

static volatile int *plat_alarm_flag;

void plat_init(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
}

double plat_seconds(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec * 1.0e-6;
}

static void plat_on_alarm(int sig)
{
    (void)sig;
    *plat_alarm_flag = 1;
}

void plat_alarm_start(int seconds, volatile int *flag)
{
    plat_alarm_flag = flag;
    signal(SIGALRM, plat_on_alarm);
    alarm((unsigned)seconds);
}

void plat_alarm_end(void)
{
    alarm(0);
    signal(SIGALRM, SIG_DFL);
}

/* PA-RISC architecture level of the running CPU as a string, NULL if it
   is not PA-RISC or can't be told. */
static const char *plat_pa_level(long *ver)
{
    *ver = 0;
#ifdef _SC_CPU_VERSION
    *ver = sysconf(_SC_CPU_VERSION);
    switch (*ver) {
    case 0x20B: return "PA-RISC 1.0";
    case 0x210: return "PA-RISC 1.1";
    case 0x211: return "PA-RISC 1.2";
    case 0x214: return "PA-RISC 2.0";
    }
#endif
    return NULL;
}

int plat_check_target(char *buf, int len)
{
    long ver;
    const char *level = plat_pa_level(&ver);
    char tmp[160];
    int ok = 1;

    sprintf(tmp, "%s%.60s", UB_TARGET_NAME, UB_COMPILER);
    if (level != NULL && ver == 0x20B) {
        strcat(tmp, " - this CPU is PA-RISC 1.0");
        ok = 0;
    }
    strncpy(buf, tmp, len - 1);
    buf[len - 1] = '\0';
    return ok;
}

void plat_sysinfo(char *buf, int len)
{
    struct utsname u;
    struct pst_dynamic dyn;
    struct pst_processor cpu;
    long ncpu = 1, ver;
    const char *level;
    char tmp[sizeof(u.sysname) + sizeof(u.release) + sizeof(u.machine) + 128];

    if (uname(&u) < 0) {
        strncpy(buf, "unknown HP-UX system", len - 1);
        buf[len - 1] = '\0';
        return;
    }
    if (pstat_getdynamic(&dyn, sizeof(dyn), 1, 0) == 1 && dyn.psd_proc_cnt > 0)
        ncpu = (long)dyn.psd_proc_cnt;

    sprintf(tmp, "%s %s; %s", u.sysname, u.release, u.machine);
    level = plat_pa_level(&ver);
    if (level != NULL)
        sprintf(tmp + strlen(tmp), "; %s", level);
    else if (ver > 0)
        sprintf(tmp + strlen(tmp), "; CPU version 0x%lx", ver);

    /* The interval timer (CR16) counts at the CPU clock on PA-RISC 1.1
       workstations; the kernel keeps its counts per clock tick. */
    if (pstat_getprocessor(&cpu, sizeof(cpu), 1, 0) == 1
        && cpu.psp_iticksperclktick > 0) {
        double mhz = (double)cpu.psp_iticksperclktick
                     * (double)sysconf(_SC_CLK_TCK) / 1.0e6;

        sprintf(tmp + strlen(tmp), "; %.1f MHz", mhz);
    }
    sprintf(tmp + strlen(tmp), "; %ld CPU%s", ncpu, ncpu == 1 ? "" : "s");
    strncpy(buf, tmp, len - 1);
    buf[len - 1] = '\0';
}
