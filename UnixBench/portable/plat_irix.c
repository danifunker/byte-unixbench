/*******************************************************************************
 *  UnixBench portable harness - SGI IRIX platform layer
 *
 *  Written for IRIX 5.3 (o32 ABI, IDO 5.3 cc or gcc) and should also build
 *  on IRIX 6.x.  Uses only calls IRIX 5.3 already had: gettimeofday(),
 *  alarm(), uname(), sysmp() and the hardware inventory (getinvent()).
 *
 *  This file is always compiled for MIPS I, whatever ISA the benchmark
 *  kernels are built for, so a MIPS II (R4000) build started on an R3000
 *  can still say so instead of dying with an illegal instruction.
 ******************************************************************************/

/* Ask for the two-argument gettimeofday() prototype.  Without it IRIX
   declares the SVR4 form, which may take only one argument. */
#define _BSD_TIME

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/sysmp.h>
#include <invent.h>
#include "plat.h"

/* CPU-specific builds are selected with -DUB_TARGET_<variant>.  The level
   is the MIPS ISA the kernels were compiled for. */
#if defined(UB_TARGET_R4000)
#define UB_TARGET_NAME  "MIPS II (R4000), -mips2"
#define UB_TARGET_ISA   2
#else
#define UB_TARGET_NAME  "generic (MIPS I)"
#define UB_TARGET_ISA   1
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
    /* SVR4 signal(): the handler is reset after one delivery, which is all
       we need since only one alarm is outstanding at a time. */
    signal(SIGALRM, plat_on_alarm);
    alarm((unsigned)seconds);
}

void plat_alarm_end(void)
{
    alarm(0);
    signal(SIGALRM, SIG_DFL);
}

/* Processor implementation number, from the PRId register as the hardware
   inventory reports it (what hinv prints as "MIPS R4400 Processor Chip
   Revision: 6.0").  The implementation is bits 15..8, the revision is
   bits 7..4 (major) and 3..0 (minor). */
#define IMP_R2000   0x01
#define IMP_R3000   0x02
#define IMP_R6000   0x03
#define IMP_R4000   0x04
#define IMP_R6000A  0x06
#define IMP_R10000  0x09
#define IMP_R12000  0x0e
#define IMP_R8000   0x10
#define IMP_R4600   0x20
#define IMP_R4700   0x21
#define IMP_R4650   0x22
#define IMP_R5000   0x23

typedef struct {
    int           found;
    unsigned long prid;     /* low 16 bits of PRId */
    long          mhz;      /* 0 if unknown */
} plat_cpu;

static void plat_get_cpu(plat_cpu *c)
{
    inventory_t *inv;

    c->found = 0;
    c->prid = 0;
    c->mhz = 0;
    if (setinvent() != 0)
        return;
    while ((inv = getinvent()) != NULL) {
        if (inv->inv_class != INV_PROCESSOR)
            continue;
        if (inv->inv_type == INV_CPUCHIP && !c->found) {
            c->prid = (unsigned long)inv->inv_state & 0xffffUL;
            c->found = 1;
        } else if (inv->inv_type == INV_CPUBOARD && c->mhz == 0) {
            c->mhz = (long)inv->inv_controller;
        }
    }
    endinvent();
}

static const char *plat_cpu_name(unsigned long prid)
{
    unsigned imp = (unsigned)((prid >> 8) & 0xff);
    unsigned maj = (unsigned)((prid >> 4) & 0xf);

    switch (imp) {
    case IMP_R2000:  return "R2000";
    case IMP_R3000:  return "R3000";
    case IMP_R6000:  return "R6000";
    case IMP_R6000A: return "R6000A";
    case IMP_R4000:  return maj >= 4 ? "R4400" : "R4000";
    case IMP_R4600:  return "R4600";
    case IMP_R4650:  return "R4650";
    case IMP_R4700:  return "R4700";
    case IMP_R5000:  return "R5000";
    case IMP_R8000:  return "R8000";
    case IMP_R10000: return "R10000";
    case IMP_R12000: return "R12000";
    }
    return NULL;
}

/* MIPS ISA level the CPU implements, as far as IRIX 5.3's o32 ABI can use
   it: everything from the R4000 on runs MIPS II code. */
static int plat_cpu_isa(unsigned long prid)
{
    unsigned imp = (unsigned)((prid >> 8) & 0xff);

    if (imp == IMP_R2000 || imp == IMP_R3000)
        return 1;
    return 2;
}

int plat_check_target(char *buf, int len)
{
    plat_cpu c;
    char tmp[128];
    const char *name;

    strncpy(buf, UB_TARGET_NAME, len - 1);
    buf[len - 1] = '\0';
    if (UB_TARGET_ISA <= 1)
        return 1;

    plat_get_cpu(&c);
    if (!c.found || plat_cpu_isa(c.prid) >= UB_TARGET_ISA)
        return 1;

    name = plat_cpu_name(c.prid);
    sprintf(tmp, "%s - this %s does not support it", UB_TARGET_NAME,
            name != NULL ? name : "CPU");
    strncpy(buf, tmp, len - 1);
    buf[len - 1] = '\0';
    return 0;
}

void plat_sysinfo(char *buf, int len)
{
    struct utsname u;
    plat_cpu c;
    long ncpu;
    const char *name;
    char chip[64];
    char tmp[sizeof(u.sysname) + sizeof(u.release) + sizeof(u.machine) + 96];

    if (uname(&u) != 0) {
        strncpy(buf, "unknown IRIX system", len - 1);
        buf[len - 1] = '\0';
        return;
    }

    ncpu = (long)sysmp(MP_NPROCS);
    if (ncpu < 1)
        ncpu = 1;

    plat_get_cpu(&c);
    if (!c.found) {
        strcpy(chip, "unknown CPU");
    } else {
        name = plat_cpu_name(c.prid);
        if (name != NULL)
            sprintf(chip, "%s rev %lu.%lu", name, (c.prid >> 4) & 0xfUL,
                    c.prid & 0xfUL);
        else
            sprintf(chip, "MIPS PRId 0x%04lx", c.prid);
        if (c.mhz > 0)
            sprintf(chip + strlen(chip), " %ld MHz", c.mhz);
    }

    sprintf(tmp, "%s %s; %s; %s; %ld CPU%s", u.sysname, u.release,
            u.machine, chip, ncpu, ncpu == 1 ? "" : "s");
    strncpy(buf, tmp, len - 1);
    buf[len - 1] = '\0';
}
