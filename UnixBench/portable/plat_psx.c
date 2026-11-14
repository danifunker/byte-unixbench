/*******************************************************************************
 *  UnixBench portable harness - POSIX platform layer
 *
 *  Two uses.  On a current host it lets the harness run next to the regular
 *  UnixBench binaries to check the scores agree (Makefile.posix).  On Unix
 *  systems that cannot run the regular harness comfortably it is the real
 *  thing: Makefile.sparc builds it for NetBSD/sparc and Solaris/sparc.
 *
 *  Plain C89 like the rest: no long long, and nothing wider than long is
 *  passed to printf (time_t is 64 bits on NetBSD, 32-bit ports included).
 ******************************************************************************/

/* -std=c89 hides gettimeofday() and friends unless asked for. */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#ifndef _NETBSD_SOURCE
#define _NETBSD_SOURCE
#endif
#ifndef __EXTENSIONS__
#define __EXTENSIONS__          /* Solaris */
#endif

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/utsname.h>
#ifdef __NetBSD__
#include <sys/param.h>
#include <sys/sysctl.h>
#endif
#if defined(__sun) && defined(__SVR4)
#include <sys/systeminfo.h>
#define UB_SOLARIS 1
#endif
#include "plat.h"

/* The SPARC builds of Makefile.sparc.  UB_TARGET_SPARCV8 marks the build
   whose kernels use the V8 multiply and divide instructions. */
#if defined(UB_TARGET_SPARCV8)
#define UB_SPARC_NEED 8
#define UB_SPARC_NAME "sparc v8 (-mcpu=supersparc)"
#elif defined(__sparc__) && !defined(__sparc_v8__) && !defined(__sparc_v9__) \
    && !defined(__arch64__)
#define UB_SPARC_NAME "sparc generic (v7)"
#endif

/* NetBSD/sparc: machdep.cpu_arch, the SPARC version (7, 8 or 9) the kernel
   found.  The number is CPU_ARCH in <machine/cpu.h>, which is not written
   for C89 user code to include. */
#if defined(__NetBSD__) && defined(__sparc__) && defined(CTL_MACHDEP)
#define UB_NBSD_CPU_ARCH 4
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

/* SPARC architecture version of the running CPU, 0 if it can't be told. */
static int plat_sparc_arch(void)
{
#ifdef UB_NBSD_CPU_ARCH
    int mib[2], arch = 0;
    size_t n = sizeof(arch);

    mib[0] = CTL_MACHDEP;
    mib[1] = UB_NBSD_CPU_ARCH;
    if (sysctl(mib, 2, &arch, &n, NULL, 0) == 0 && n == sizeof(arch))
        return arch;
#endif
#if defined(UB_SOLARIS) && defined(__sparc__) && defined(SI_ISALIST)
    /* Solaris 2.6 and later: the instruction sets the CPU runs natively,
       best first, e.g. "sparcv8 sparcv8-fsmuld sparcv7 sparc". */
    char isa[256];
    long n = sysinfo(SI_ISALIST, isa, sizeof(isa));

    if (n > 0 && n <= (long)sizeof(isa)) {
        if (strstr(isa, "sparcv9") != NULL)
            return 9;
        if (strstr(isa, "sparcv8") != NULL)
            return 8;
        if (strstr(isa, "sparcv7") != NULL)
            return 7;
    }
#endif
    return 0;
}

int plat_check_target(char *buf, int len)
{
    int ok = 1;
#ifdef UB_SPARC_NAME
    char tmp[96];

    sprintf(tmp, "%s", UB_SPARC_NAME);
#if defined(__GNUC__) && !defined(__clang__)
    sprintf(tmp + strlen(tmp), ", gcc %.40s", __VERSION__);
#endif
#ifdef UB_SPARC_NEED
    {
        int arch = plat_sparc_arch();

        if (arch != 0 && arch < UB_SPARC_NEED) {
            sprintf(tmp + strlen(tmp), " - this CPU is SPARC v%d", arch);
            ok = 0;
        }
    }
#endif
    strncpy(buf, tmp, len - 1);
#else
    strncpy(buf, "generic", len - 1);
#endif
    buf[len - 1] = '\0';
    return ok;
}

void plat_sysinfo(char *buf, int len)
{
    struct utsname u;
#ifdef _SC_NPROCESSORS_ONLN
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
#else
    long ncpu = 1;
#endif
    char model[80];
    char tmp[sizeof(u.sysname) + sizeof(u.release) + sizeof(u.machine)
             + sizeof(model) + 64];
    int arch = plat_sparc_arch();

    if (uname(&u) < 0) {            /* success is 0, on Solaris 1 */
        strncpy(buf, "unknown POSIX system", len - 1);
        buf[len - 1] = '\0';
        return;
    }
    if (ncpu < 1)
        ncpu = 1;
    sprintf(tmp, "%s %s; %s", u.sysname, u.release, u.machine);
#if defined(__NetBSD__) && defined(HW_MODEL)
    {
        int mib[2];
        size_t n = sizeof(model) - 1;

        mib[0] = CTL_HW;
        mib[1] = HW_MODEL;
        if (sysctl(mib, 2, model, &n, NULL, 0) == 0 && n > 0) {
            model[n] = '\0';
            sprintf(tmp + strlen(tmp), "; %s", model);
        }
    }
#endif
#if defined(UB_SOLARIS) && defined(SI_PLATFORM)
    {
        /* e.g. "SUNW,SPARCstation-20" */
        long n = sysinfo(SI_PLATFORM, model, sizeof(model));

        if (n > 1 && n <= (long)sizeof(model))
            sprintf(tmp + strlen(tmp), "; %s", model);
    }
#endif
    if (arch != 0)
        sprintf(tmp + strlen(tmp), "; SPARC v%d", arch);
    sprintf(tmp + strlen(tmp), "; %ld CPU%s", ncpu, ncpu == 1 ? "" : "s");
    strncpy(buf, tmp, len - 1);
    buf[len - 1] = '\0';
}
