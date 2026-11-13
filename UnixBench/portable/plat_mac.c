/*******************************************************************************
 *  UnixBench portable harness - classic Mac OS platform layer
 *
 *  Built with Retro68 (GCC 12 cross-compiler with the Multiversal
 *  Interfaces) for 68k and PowerPC; build-mac.sh merges the two into one
 *  fat application.  Runs on System 6 and later on 68k, and on System 7.1.2
 *  and later on PowerPC.  Also compiles for the 68k emulator of a PowerPC
 *  Mac, where the Build: line warns that the scores are emulated.
 *
 *  Console.  mac_console.c turns stdout into a window and stdin into a
 *  line editor.  There is no command line, so plat_init() asks for one on
 *  the console (Return alone keeps the defaults), the way CodeWarrior's
 *  ccommand() does.  Everything printed is also copied to the
 *  file "UnixBench Results" next to the application (or on the desktop of
 *  the startup disk if the application's folder is locked, e.g. a CD-ROM):
 *  plat_init() replaces stdout with a tee stream (newlib's funopen()), so
 *  the runner needs no changes.  At exit the window stays open until Return
 *  is pressed or its close box is clicked.
 *
 *  Clock.  Microseconds() where the trap exists (System 7 and later), else
 *  TickCount() at 60.15 Hz.  The 64-bit microsecond count is combined in
 *  double arithmetic; no 64-bit integer type is used.
 *
 *  Alarm.  A Time Manager task: InsTime/PrimeTime(milliseconds) arm it and
 *  RmvTime releases it.  The task routine runs at interrupt time and only
 *  stores to the flag.  On 68k the Time Manager passes the task record in
 *  A1 and A5 (the application globals register) is not guaranteed, so the
 *  record is extended with the flag pointer and the routine takes its
 *  argument in A1 (Retro68's #pragma parameter) and touches no globals.
 *  Retro68 code does not address globals through A5 in the first place -
 *  Elf2Mac emits absolute addresses that the start-up code relocates - so
 *  the A5 issue does not arise here, but the routine is written to be safe
 *  under either model.  On PowerPC the routine is reached through a Mixed
 *  Mode routine descriptor, which hands A1 over as the C argument.
 *  Multiversal's TMTask lacks the revised Time Manager's tmWakeUp and
 *  tmReserved fields, so the full 22-byte record is declared here.
 *  plat_init() fires a 50 ms test alarm; if it never arrives the alarm is
 *  marked broken and plat_alarm_start() sets the flag at once, like
 *  plat_nt.c does when CreateThread fails, rather than spin forever.
 *
 *  System info.  Gestalt for the system version, CPU, FPU, RAM, machine
 *  name and the 68k-emulation check; SysEnvirons on System 6 without
 *  Gestalt.  Traps that may be missing on System 6 (Gestalt, Microseconds,
 *  FindFolder) are checked with NGetTrapAddress before use.
 ******************************************************************************/

#define _DEFAULT_SOURCE         /* newlib: funopen() */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <MacTypes.h>
#include <Events.h>
#include <Files.h>
#include <Gestalt.h>
#include <MixedMode.h>
#include <OSUtils.h>
#include <TextUtils.h>
#include <Timer.h>
#include <Traps.h>
#include "plat.h"

/* Four-character codes without multi-character constants. */
#define UB_FOURCC(a, b, c, d) \
    (((long)(a) << 24) | ((long)(b) << 16) | ((long)(c) << 8) | (long)(d))

#define UB_TRAP_MICROSECONDS    0xA193
#define UB_TRAP_GESTALT         0xA1AD
#define UB_TRAP_FINDFOLDER      0xA823
#define UB_TRAP_UNIMPLEMENTED   0xA89F
#define UB_MACHINE_NAME_STRID   (-16395)        /* kMachineNameStrID */
#define UB_ON_SYSTEM_DISK       (-32768)        /* kOnSystemDisk */
#define UB_TIMER_PROCINFO       0x0000B802L     /* uppTimerProcInfo */

#define UB_RESULTS_NAME "UnixBench Results"

/* Which CPU the kernels were compiled for, from the compiler's own macros.
   build-mac.sh passes -m68020 -m68881 for the FPU build.  Level is the
   gestaltNativeCPUtype value: 0 = 68000 ... 4 = 68040. */
#if TARGET_CPU_PPC
#define UB_TARGET_CPU   0
#define UB_TARGET_FPU   0
#define UB_TARGET_NAME  "PowerPC generic"
#else
#if defined(__mc68040__)
#define UB_TARGET_CPU   4
#define UB_CPU_STR      "68040"
#elif defined(__mc68030__)
#define UB_TARGET_CPU   3
#define UB_CPU_STR      "68030"
#elif defined(__mc68020__)
#define UB_TARGET_CPU   2
#define UB_CPU_STR      "68020"
#else
#define UB_TARGET_CPU   0
#define UB_CPU_STR      "generic"
#endif
#ifdef __HAVE_68881__
#define UB_TARGET_FPU   1
#define UB_TARGET_NAME  "68k " UB_CPU_STR "+FPU"
#else
#define UB_TARGET_FPU   0
#define UB_TARGET_NAME  "68k " UB_CPU_STR
#endif
#endif

/* ---------------------------------------------------------------- system */

typedef struct {
    int  probed;
    long sysver;        /* gestaltSystemVersion, e.g. 0x0755; 0 unknown */
    long cpu;           /* gestaltNativeCPUtype numbering; -1 unknown */
    long fpu;           /* 0 none, 1 68881, 2 68882, 3 68040; -1 unknown */
    long ram_mb;        /* 0 unknown */
    long ncpu;
    int  emulated;      /* this is 68k code running on a PowerPC */
    char machine[64];
} plat_sysrec;

static plat_sysrec plat_sys;
static int plat_have_usec;          /* Microseconds() trap present */
static int plat_have_gestalt;
static int plat_have_findfolder;
static int plat_tm_broken;          /* Time Manager alarm never fired */

static void plat_c2p(const char *c, unsigned char *p)
{
    size_t n = strlen(c);

    if (n > 255)
        n = 255;
    p[0] = (unsigned char)n;
    memcpy(p + 1, c, n);
}

/* Pascal to C, dropping leading blanks: System 7.5's machine-name strings
   start with two spaces. */
static void plat_p2c(const unsigned char *p, char *c, int len)
{
    int n = p[0], skip = 0;

    while (skip < n && p[1 + skip] == ' ')
        ++skip;
    n -= skip;
    if (n > len - 1)
        n = len - 1;
    memcpy(c, p + 1 + skip, n);
    c[n] = '\0';
}

#if TARGET_CPU_68K
/* Inside Macintosh's TrapAvailable(): a trap exists if its address differs
   from that of _Unimplemented. */
static int plat_trap_ok(unsigned short trap)
{
    TrapType t = (trap & 0x0800) ? (TrapType)kToolboxTrapType
                                 : (TrapType)kOSTrapType;

    return NGetTrapAddress(trap, t)
        != NGetTrapAddress(UB_TRAP_UNIMPLEMENTED, (TrapType)kToolboxTrapType);
}
#endif

static int plat_gestalt(OSType selector, long *out)
{
    SInt32 r = 0;

    if (!plat_have_gestalt || Gestalt(selector, &r) != noErr)
        return 0;
    *out = r;
    return 1;
}

static const char *plat_cpu_name(long cpu)
{
    switch (cpu) {
    case 0:     return "68000";
    case 1:     return "68010";
    case 2:     return "68020";
    case 3:     return "68030";
    case 4:     return "68040";
    case 0x101: return "PowerPC 601";
    case 0x103: return "PowerPC 603";
    case 0x104: return "PowerPC 604";
    case 0x106: return "PowerPC 603e";
    case 0x107: return "PowerPC 603ev";
    case 0x108: return "PowerPC 750 (G3)";
    case 0x109: return "PowerPC 604e";
    case 0x10A: return "PowerPC 604ev";
    case 0x10C: return "PowerPC 7400 (G4)";
    case 0x110: return "PowerPC 7450 (G4)";
    case 0x111: return "PowerPC 7455 (G4)";
    case 0x112: return "PowerPC 7447 (G4)";
    case 0x139: return "PowerPC 970 (G5)";
    case 0x13C: return "PowerPC 970FX (G5)";
    case 0x144: return "PowerPC 970MP (G5)";
    default:    return NULL;
    }
}

static void plat_cpu_str(long cpu, char *buf)
{
    const char *name = plat_cpu_name(cpu);

    if (name != NULL)
        strcpy(buf, name);
    else if (cpu < 0)
        strcpy(buf, "unknown CPU");
    else if (cpu >= 0x100)
        sprintf(buf, "PowerPC (type 0x%lX)", cpu);
    else
        sprintf(buf, "68k (type %ld)", cpu);
}

/* "System 7.5.5", "Mac OS 9.2.2" from the BCD version word. */
static void plat_sysver_str(long v, char *buf)
{
    int major, minor, bug;

    if (v <= 0) {
        strcpy(buf, "System ?");
        return;
    }
    major = (int)(((v >> 12) & 0xF) * 10 + ((v >> 8) & 0xF));
    minor = (int)((v >> 4) & 0xF);
    bug = (int)(v & 0xF);
    if (bug != 0)
        sprintf(buf, "%s %d.%d.%d", major >= 8 ? "Mac OS" : "System",
                major, minor, bug);
    else
        sprintf(buf, "%s %d.%d", major >= 8 ? "Mac OS" : "System",
                major, minor);
}

#if TARGET_CPU_68K
/* SysEnvirons machine types, for System 6 without Gestalt. */
static const char *plat_env_machine(int type)
{
    switch (type) {
    case 1:  return "Macintosh 512K enhanced";
    case 2:  return "Macintosh Plus";
    case 3:  return "Macintosh SE";
    case 4:  return "Macintosh II";
    case 5:  return "Macintosh IIx";
    case 6:  return "Macintosh IIcx";
    case 7:  return "Macintosh SE/30";
    case 8:  return "Macintosh Portable";
    case 9:  return "Macintosh IIci";
    case 11: return "Macintosh IIfx";
    default: return "Macintosh";
    }
}
#endif

static void plat_probe(void)
{
    long v;
    unsigned char pstr[256];

    if (plat_sys.probed)
        return;
    plat_sys.probed = 1;
    plat_sys.cpu = -1;
    plat_sys.fpu = -1;
    plat_sys.ncpu = 1;

#if TARGET_CPU_68K
    plat_have_gestalt = plat_trap_ok(UB_TRAP_GESTALT);
    plat_have_usec = plat_trap_ok(UB_TRAP_MICROSECONDS);
    plat_have_findfolder = plat_trap_ok(UB_TRAP_FINDFOLDER);
#else
    plat_have_gestalt = 1;
    plat_have_usec = 1;
    plat_have_findfolder = 1;
#endif

    if (plat_gestalt(gestaltSystemVersion, &v))
        plat_sys.sysver = v;
    /* gestaltNativeCPUtype (System 7.5) counts 68k CPUs from 0 and
       reports the real CPU under the 68k emulator; the older
       gestaltProcessorType counts from 1 and reports the emulated 68020. */
    if (plat_gestalt(gestaltNativeCPUtype, &v))
        plat_sys.cpu = v;
    else if (plat_gestalt(gestaltProcessorType, &v))
        plat_sys.cpu = v < 0x100 ? v - 1 : v;
    if (plat_gestalt(gestaltFPUType, &v))
        plat_sys.fpu = v;
    if (plat_gestalt(gestaltPhysicalRAMSize, &v))
        plat_sys.ram_mb = (long)((unsigned long)v >> 20);
#if TARGET_CPU_68K
    if (plat_gestalt(gestaltSysArchitecture, &v))
        plat_sys.emulated = (v == gestaltPowerPC);
#endif
    if (plat_gestalt(UB_FOURCC('c', 'p', 'u', 's'), &v) && v > 0)
        plat_sys.ncpu = v;
    if (plat_gestalt(gestaltMachineType, &v)) {
        pstr[0] = 0;
        GetIndString(pstr, UB_MACHINE_NAME_STRID, (short)v);
        plat_p2c(pstr, plat_sys.machine, sizeof(plat_sys.machine));
    }

#if TARGET_CPU_68K
    if (!plat_have_gestalt) {
        SysEnvRec env;

        memset(&env, 0, sizeof(env));
        if (SysEnvirons(1, &env) == noErr) {
            plat_sys.sysver = env.systemVersion;
            if (env.processor > 0)
                plat_sys.cpu = env.processor - 1;   /* env68000 = 1 */
            plat_sys.fpu = env.hasFPU ? 1 : 0;
            strcpy(plat_sys.machine, plat_env_machine(env.machineType));
        }
    }
#endif
    if (plat_sys.machine[0] == '\0')
        strcpy(plat_sys.machine, "Macintosh");
}

void plat_sysinfo(char *buf, int len)
{
    char tmp[400], ver[32], cpu[48];

    plat_probe();
    plat_sysver_str(plat_sys.sysver, ver);
    plat_cpu_str(plat_sys.cpu, cpu);
    sprintf(tmp, "%s; %s; %s", ver, plat_sys.machine, cpu);
    if (plat_sys.emulated) {
        strcat(tmp, " running 68k code in emulation");
    } else if (plat_sys.cpu >= 0 && plat_sys.cpu < 0x100) {
        switch (plat_sys.fpu) {
        case 0:  strcat(tmp, ", no FPU"); break;
        case 1:  strcat(tmp, " + 68881"); break;
        case 2:  strcat(tmp, " + 68882"); break;
        case 3:  strcat(tmp, " with FPU"); break;
        default: break;
        }
    }
    if (plat_sys.ram_mb > 0)
        sprintf(tmp + strlen(tmp), "; %ld MB RAM", plat_sys.ram_mb);
    if (plat_sys.ncpu > 1)
        sprintf(tmp + strlen(tmp), "; %ld CPUs (1 used)", plat_sys.ncpu);
    else
        strcat(tmp, "; 1 CPU");
    sprintf(tmp + strlen(tmp), "; clock %s",
            plat_have_usec ? "Microseconds" : "TickCount (16.7 ms)");
    if (plat_tm_broken)
        strcat(tmp, "; ALARM FAILED - tests stop at once");
    strncpy(buf, tmp, len - 1);
    buf[len - 1] = '\0';
}

int plat_check_target(char *buf, int len)
{
    int ok = 1;
#if TARGET_CPU_68K
    char tmp[200];
#if UB_TARGET_CPU > 0 || UB_TARGET_FPU
    char cpu[48];
    long level;
#endif
#endif

    plat_probe();
    strncpy(buf, UB_TARGET_NAME, len - 1);
    buf[len - 1] = '\0';

#if TARGET_CPU_68K
#if UB_TARGET_CPU > 0 || UB_TARGET_FPU
    /* The 68k emulator of a PowerPC Mac is a 68020-class CPU without an
       FPU; gestaltFPUType says so, unless SoftwareFPU is installed. */
    level = plat_sys.cpu;
    if (plat_sys.emulated || level >= 0x100)
        level = 2;
    if (level >= 0 && level < UB_TARGET_CPU)
        ok = 0;
    if (UB_TARGET_FPU && plat_sys.fpu == 0)
        ok = 0;
    if (!ok) {
        plat_cpu_str(plat_sys.cpu, cpu);
        sprintf(tmp, "%s - this %s%s does not support it", UB_TARGET_NAME,
                cpu, (UB_TARGET_FPU && plat_sys.fpu == 0) ? " without FPU"
                                                           : "");
        strncpy(buf, tmp, len - 1);
        buf[len - 1] = '\0';
        return 0;
    }
#endif
    if (plat_sys.emulated) {
        sprintf(tmp, "%s; 68k code emulated on a PowerPC - scores are not"
                " native", UB_TARGET_NAME);
        strncpy(buf, tmp, len - 1);
        buf[len - 1] = '\0';
    }
#endif
    return ok;
}

/* ---------------------------------------------------------------- clock */

#if TARGET_CPU_68K
/* _Microseconds returns the high longword in A0 and the low one in D0;
   this is the glue the Universal Interfaces inline after the trap.  OS
   traps may clobber D0-D2 and A0-A1. */
static void plat_microseconds(unsigned long *hi, unsigned long *lo)
{
    unsigned long h, l;

    __asm__ __volatile__(
        ".short 0xA193\n\t"
        "move.l %%a0,%0\n\t"
        "move.l %%d0,%1"
        : "=d"(h), "=d"(l)
        :
        : "d0", "d1", "d2", "a0", "a1", "cc", "memory");
    *hi = h;
    *lo = l;
}
#else
/* UnsignedWide and Microseconds() are missing from the Multiversal
   Interfaces; InterfaceLib exports the function. */
typedef struct {
    unsigned long hi;
    unsigned long lo;
} plat_uwide;

extern pascal void Microseconds(plat_uwide *microTickCount);

static void plat_microseconds(unsigned long *hi, unsigned long *lo)
{
    plat_uwide w;

    Microseconds(&w);
    *hi = w.hi;
    *lo = w.lo;
}
#endif

double plat_seconds(void)
{
    unsigned long hi, lo;

    if (plat_have_usec) {
        plat_microseconds(&hi, &lo);
        return ((double)hi * 4294967296.0 + (double)lo) * 1.0e-6;
    }
    /* Wraps after 2.3 years of uptime; a pass never spans that. */
    return (double)TickCount() / 60.15;
}

/* ---------------------------------------------------------------- alarm */

/* The revised Time Manager's TMTask (22 bytes, 68k alignment on both
   CPUs), followed by the pointer the task routine stores to. */
#pragma pack(push, 2)
typedef struct {
    QElemPtr qLink;
    short    qType;
    ProcPtr  tmAddr;
    long     tmCount;
    long     tmWakeUp;
    long     tmReserved;
} plat_tmtask;

typedef struct {
    plat_tmtask    tm;
    volatile int  *flag;
} plat_tmext;
#pragma pack(pop)

static plat_tmext plat_task;
static int plat_task_inserted;
#if TARGET_CPU_PPC
static UniversalProcPtr plat_tm_upp;
#endif

/* Interrupt time: only the store below.  On 68k the argument arrives in
   A1; no global is referenced, so A5 does not matter. */
#if TARGET_CPU_68K
#pragma parameter plat_tm_proc(__A1)
#endif
static void plat_tm_proc(plat_tmext *task);

static void plat_tm_proc(plat_tmext *task)
{
    *task->flag = 1;
}

static void plat_alarm_arm(long ms, volatile int *flag)
{
    plat_task.flag = flag;
    if (plat_tm_broken) {
        /* Can't time the test; stop it at once rather than spin forever. */
        *flag = 1;
        return;
    }
    plat_task.tm.qLink = NULL;
    plat_task.tm.qType = 0;
#if TARGET_CPU_PPC
    plat_task.tm.tmAddr = (ProcPtr)plat_tm_upp;
#else
    plat_task.tm.tmAddr = (ProcPtr)plat_tm_proc;
#endif
    plat_task.tm.tmCount = 0;
    plat_task.tm.tmWakeUp = 0;
    plat_task.tm.tmReserved = 0;
    InsTime((QElemPtr)&plat_task);
    plat_task_inserted = 1;
    PrimeTime((QElemPtr)&plat_task, ms);
}

void plat_alarm_start(int seconds, volatile int *flag)
{
    plat_alarm_arm((long)seconds * 1000L, flag);
}

void plat_alarm_end(void)
{
    if (plat_task_inserted) {
        RmvTime((QElemPtr)&plat_task);
        plat_task_inserted = 0;
    }
}

/* Fire a 50 ms alarm and wait up to 3 seconds for it. */
static void plat_alarm_selftest(void)
{
    volatile int fired = 0;
    unsigned long t0 = TickCount();

#if TARGET_CPU_PPC
    plat_tm_upp = NewRoutineDescriptor((ProcPtr)plat_tm_proc,
                                       UB_TIMER_PROCINFO,
                                       GetCurrentArchitecture());
    if (plat_tm_upp == NULL) {
        plat_tm_broken = 1;
        return;
    }
#endif
    plat_alarm_arm(50, &fired);
    while (!fired && TickCount() - t0 < 180)
        ;
    plat_alarm_end();
    if (!fired)
        plat_tm_broken = 1;
}

/* ---------------------------------------------------------------- output */

static FILE *plat_console;          /* the console, formerly stdout */
static FILE *plat_tee;
static short plat_res_ref;          /* results file; 0 = none */
static short plat_res_vref;
static const char *plat_res_note;

static int plat_results_create(short vref, long dirid)
{
    unsigned char name[32];
    short ref;
    OSErr err;

    plat_c2p(UB_RESULTS_NAME, name);
    err = HCreate(vref, dirid, name, UB_FOURCC('t', 't', 'x', 't'),
                  UB_FOURCC('T', 'E', 'X', 'T'));
    if (err != noErr && err != dupFNErr)
        return 0;
    if (HOpen(vref, dirid, name, fsWrPerm, &ref) != noErr)
        return 0;
    SetEOF(ref, 0);
    plat_res_ref = ref;
    plat_res_vref = vref;
    return 1;
}

static void plat_results_open(void)
{
    short vref;
    SInt32 dirid;

    if (plat_results_create(0, 0)) {
        plat_res_note = "Results are also written to \"" UB_RESULTS_NAME
                        "\" next to the application.";
    } else if (plat_have_findfolder
               && FindFolder(UB_ON_SYSTEM_DISK, kDesktopFolderType, 1,
                             &vref, &dirid) == noErr
               && plat_results_create(vref, dirid)) {
        plat_res_note = "Results are also written to \"" UB_RESULTS_NAME
                        "\" on the desktop of the startup disk\n(the "
                        "application's folder is not writable).";
    } else {
        plat_res_note = "The results file could not be created; copy the "
                        "text from this window.";
    }
    printf("%s\n\n", plat_res_note);
}

/* Classic Mac text files end lines with CR. */
static void plat_results_write(const char *buf, int n)
{
    char tmp[128];
    SInt32 count;
    int i, k = 0;

    for (i = 0; i < n; ++i) {
        tmp[k++] = buf[i] == '\n' ? '\r' : buf[i];
        if (k == (int)sizeof(tmp) || i == n - 1) {
            count = k;
            FSWrite(plat_res_ref, &count, tmp);
            k = 0;
        }
    }
}

static void plat_results_close(void)
{
    if (plat_res_ref != 0) {
        FSClose(plat_res_ref);
        FlushVol(NULL, plat_res_vref);
        plat_res_ref = 0;
    }
}

static int plat_tee_write(void *cookie, const char *buf, int n)
{
    (void)cookie;
    if (plat_console != NULL) {
        fwrite(buf, 1, (size_t)n, plat_console);
        fflush(plat_console);
    }
    if (plat_res_ref != 0)
        plat_results_write(buf, n);
    return n;
}

/* Replace stdout with a stream that writes to both the console and the
   results file.  newlib's stdout is an assignable pointer. */
static void plat_tee_install(void)
{
    fflush(stdout);
    plat_tee = funopen(NULL, NULL, plat_tee_write, NULL, NULL);
    if (plat_tee == NULL)
        return;
    setvbuf(plat_tee, NULL, _IONBF, 0);
    plat_console = stdout;
    stdout = plat_tee;
}

static void plat_at_exit(void)
{
    char line[16];

    fflush(stdout);
    plat_results_close();
    if (plat_console != NULL) {
        stdout = plat_console;
        fclose(plat_tee);
        plat_tee = NULL;
        plat_console = NULL;
    }
    printf("\n%s\n\nPress Return (or close this window) to quit.\n",
           plat_res_note != NULL ? plat_res_note : "");
    fflush(stdout);
    fgets(line, sizeof(line), stdin);
}

/* ---------------------------------------------------------------- init */

void plat_init(int *argc, char ***argv)
{
    static char line[256];
    static char *args[32];
    int n = 0;
    char *p;

    printf("UnixBench for classic Mac OS\n\n");
    printf("Type options and press Return; Return alone runs every test"
           " with the\ndefaults.  Options: -q quick check, -v show every"
           " pass, -i N passes,\n-f force, and test names (see -h).\n> ");
    fflush(stdout);

    args[n++] = (*argv != NULL && (*argv)[0] != NULL) ? (*argv)[0]
                                                        : (char *)"ubench";
    if (fgets(line, sizeof(line), stdin) != NULL) {
        p = strtok(line, " \t\r\n");
        while (p != NULL && n < (int)(sizeof(args) / sizeof(args[0])) - 1) {
            args[n++] = p;
            p = strtok(NULL, " \t\r\n");
        }
    }
    args[n] = NULL;
    *argc = n;
    *argv = args;
    printf("\n");

    plat_probe();
    plat_alarm_selftest();
    plat_results_open();
    plat_tee_install();
    atexit(plat_at_exit);
}
