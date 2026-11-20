/*******************************************************************************
 *  UnixBench - ubrun: a C stand-in for the Perl "Run" script
 *
 *  For Unix systems that have fork/exec/pipe but no Perl 5 (HP-UX 10.20
 *  ships Perl 4).  It runs the stock test programs built from ../src, one
 *  copy each, the way Run does: each pass is a process whose stderr carries
 *  "COUNT|count|timebase|unit" (and maybe "TIME|seconds"); the wall-clock
 *  time is measured from outside; the lowest third of the passes is dropped
 *  and the geometric mean of the rest is indexed against the SPARCstation
 *  20-61 baseline of ../pgms/index.base.
 *
 *  The twelve tests are the "System Benchmarks Index" set of Run, with
 *  Run's durations and pass counts.  Not done: parallel copies (-c), the
 *  graphics tests, the log and HTML files.
 *
 *  Everything lives in one directory: the test programs, multi.sh, tst.sh
 *  and sort.src (Makefile.hpux's "sys" target sets it up).
 *
 *  usage: ubrun [-v] [-q] [-i passes] [-d scratchdir] [test ...]
 *
 *  Plain C89 and POSIX.1.
 ******************************************************************************/

#ifndef _HPUX_SOURCE
#define _HPUX_SOURCE
#endif
#ifndef __EXTENSIONS__
#define __EXTENSIONS__
#endif
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/utsname.h>

#define UB_VERSION      "6.0.1-ubrun"
#define UB_LONG_PASSES  10
#define UB_SHORT_PASSES 3
#define UB_MAX_PASSES   100
#define UB_PATH         1024
#define UB_MAXARGS      12

typedef struct {
    const char *name;       /* as in Run and index.base */
    const char *desc;       /* Run's "logmsg" */
    const char *unit;
    const char *prog;
    const char *args;       /* space separated; %T seconds, %D scratch
                               directory, %B program directory */
    int         seconds;    /* Run's duration */
    int         long_run;   /* Run's "repeat" => 'long' */
    double      baseline;
} ub_test;

static const ub_test ub_tests[] = {
    { "dhry2reg", "Dhrystone 2 using register variables", "lps",
      "dhry2reg", "%T", 10, 1, 116700.0 },
    { "whetstone-double", "Double-Precision Whetstone", "MWIPS",
      "whetstone-double", "", 10, 1, 55.0 },
    { "execl", "Execl Throughput", "lps",
      "execl", "%T", 30, 0, 43.0 },
    { "fstime", "File Copy 1024 bufsize 2000 maxblocks", "KBps",
      "fstime", "-c -t %T -d %D -b 1024 -m 2000", 30, 0, 3960.0 },
    { "fsbuffer", "File Copy 256 bufsize 500 maxblocks", "KBps",
      "fstime", "-c -t %T -d %D -b 256 -m 500", 30, 0, 1655.0 },
    { "fsdisk", "File Copy 4096 bufsize 8000 maxblocks", "KBps",
      "fstime", "-c -t %T -d %D -b 4096 -m 8000", 30, 0, 5800.0 },
    { "pipe", "Pipe Throughput", "lps",
      "pipe", "%T", 10, 1, 12440.0 },
    { "context1", "Pipe-based Context Switching", "lps",
      "context1", "%T", 10, 1, 4000.0 },
    { "spawn", "Process Creation", "lps",
      "spawn", "%T", 30, 0, 126.0 },
    { "shell1", "Shell Scripts (1 concurrent)", "lpm",
      "looper", "%T %B/multi.sh 1", 60, 0, 42.4 },
    { "shell8", "Shell Scripts (8 concurrent)", "lpm",
      "looper", "%T %B/multi.sh 8", 60, 0, 6.0 },
    { "syscall", "System Call Overhead", "lps",
      "syscall", "%T", 10, 1, 15000.0 }
};
#define UB_NTESTS ((int)(sizeof(ub_tests) / sizeof(ub_tests[0])))

static int  ub_verbose;
static char ub_bindir[UB_PATH];
static char ub_tmpdir[UB_PATH];

static double ub_now(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec * 1.0e-6;
}

/* Expand one argument: %T, %D and %B. */
static void ub_expand(char *out, const char *in, int n, int seconds)
{
    char num[16];
    int o = 0;

    while (n > 0 && o < UB_PATH - 1) {
        const char *rep = NULL;

        if (in[0] == '%' && n > 1) {
            if (in[1] == 'T') {
                sprintf(num, "%d", seconds);
                rep = num;
            } else if (in[1] == 'D') {
                rep = ub_tmpdir;
            } else if (in[1] == 'B') {
                rep = ub_bindir;
            }
        }
        if (rep != NULL) {
            while (*rep != '\0' && o < UB_PATH - 1)
                out[o++] = *rep++;
            in += 2;
            n -= 2;
        } else {
            out[o++] = *in++;
            --n;
        }
    }
    out[o] = '\0';
}

/* Run one pass.  Returns 0 and the pass's score, or -1. */
static int ub_pass(const ub_test *t, int seconds, double *score,
                   double *elapsed)
{
    static char argbuf[UB_MAXARGS][UB_PATH];
    static char out[8192];
    char *argv[UB_MAXARGS + 2];
    char prog[UB_PATH + 64];
    const char *p;
    char *line, *next;
    int argc = 0, fd[2], status, got = 0, len = 0, n;
    double count = 0.0, timebase = 0.0, selftime = 0.0, t0, t1;
    pid_t pid;

    sprintf(prog, "%s/%s", ub_bindir, t->prog);
    argv[argc++] = prog;
    for (p = t->args; *p != '\0' && argc <= UB_MAXARGS; ) {
        const char *e;

        while (*p == ' ')
            ++p;
        if (*p == '\0')
            break;
        for (e = p; *e != '\0' && *e != ' '; ++e)
            ;
        ub_expand(argbuf[argc - 1], p, (int)(e - p), seconds);
        argv[argc] = argbuf[argc - 1];
        ++argc;
        p = e;
    }
    argv[argc] = NULL;

    if (pipe(fd) != 0) {
        perror("pipe");
        return -1;
    }
    fflush(stdout);
    t0 = ub_now();
    pid = fork();
    if (pid < 0) {
        perror("fork");
        close(fd[0]);
        close(fd[1]);
        return -1;
    }
    if (pid == 0) {
        int nul = open("/dev/null", 1);

        close(fd[0]);
        dup2(fd[1], 2);                 /* results come on stderr */
        if (nul >= 0)
            dup2(nul, 1);
        close(fd[1]);
        execv(prog, argv);
        fprintf(stderr, "cannot run %s: %s\n", prog, strerror(errno));
        _exit(127);
    }
    close(fd[1]);
    while ((n = (int)read(fd[0], out + len, sizeof(out) - 1 - len)) != 0) {
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        len += n;
        if (len >= (int)sizeof(out) - 1)
            len = 0;                    /* chatter; keep the tail */
    }
    t1 = ub_now();
    close(fd[0]);
    out[len] = '\0';
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;

    for (line = out; *line != '\0'; line = next) {
        next = strchr(line, '\n');
        if (next != NULL)
            *next++ = '\0';
        else
            next = line + strlen(line);
        if (strncmp(line, "COUNT|", 6) == 0) {
            if (sscanf(line + 6, "%lf|%lf", &count, &timebase) == 2)
                got = 1;
        } else if (strncmp(line, "TIME|", 5) == 0) {
            sscanf(line + 5, "%lf", &selftime);
        } else if (*line != '\0' && (ub_verbose || status != 0)) {
            printf("\n    %s: %s", t->prog, line);
        }
    }
    if (status != 0 || !got) {
        printf("\n    %s: %s (status 0x%x)", t->prog,
               got ? "failed" : "no COUNT line", (unsigned)status);
        return -1;
    }

    *elapsed = selftime > 0.0 ? selftime : t1 - t0;
    if (timebase > 0.0 && *elapsed > 0.0)
        *score = count / (*elapsed / timebase);
    else
        *score = count;                 /* already a rate */
    return 0;
}

static int ub_cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Run's combinePassResults(): sort, drop the lowest third, geometric mean
   of the rest.  Returns the passes kept, 0 on failure. */
static int ub_run_test(const ub_test *t, int passes, int seconds,
                       double *result)
{
    double scores[UB_MAX_PASSES], elapsed, logsum = 0.0;
    int i, ndump, kept = 0;

    printf("%-40s", t->desc);
    fflush(stdout);
    for (i = 0; i < passes; ++i) {
        if (ub_pass(t, seconds, &scores[i], &elapsed) != 0) {
            printf("\n");
            return 0;
        }
        if (ub_verbose)
            printf("\n    pass %2d: %14.1f %s in %.3f s",
                   i + 1, scores[i], t->unit, elapsed);
        else
            printf(" %d", i + 1);
        fflush(stdout);
    }
    printf("\n");

    qsort(scores, passes, sizeof(scores[0]), ub_cmp_double);
    ndump = passes / 3;
    for (i = ndump; i < passes; ++i) {
        if (scores[i] > 0.0)
            logsum += log(scores[i]);
        ++kept;
    }
    *result = exp(logsum / kept);
    return kept;
}

static const ub_test *ub_find(const char *name)
{
    int i;

    for (i = 0; i < UB_NTESTS; ++i)
        if (strcmp(ub_tests[i].name, name) == 0)
            return &ub_tests[i];
    return NULL;
}

static void ub_usage(void)
{
    int i;

    printf("usage: ubrun [-v] [-q] [-i passes] [-d scratchdir] [test ...]"
           "\n\n");
    printf("  -v         show each pass and the tests' other output\n");
    printf("  -q         quick check: short runs, 1 pass\n");
    printf("  -i passes  passes for the long tests (default %d)\n",
           UB_LONG_PASSES);
    printf("  -d dir     directory for the file tests' scratch files\n"
           "             (default: tmp next to the programs)\n\n");
    printf("tests (default: all):\n");
    for (i = 0; i < UB_NTESTS; ++i)
        printf("  %-18s %s\n", ub_tests[i].name, ub_tests[i].desc);
}

/* Make a path absolute: the tests run with a different working directory. */
static int ub_abspath(char *out, const char *in)
{
    char cwd[UB_PATH];

    if (in[0] == '/') {
        if (strlen(in) >= UB_PATH - 64)
            return -1;
        strcpy(out, in);
        return 0;
    }
    if (getcwd(cwd, sizeof(cwd)) == NULL
        || strlen(cwd) + strlen(in) + 2 >= UB_PATH - 64)
        return -1;
    if (strcmp(in, ".") == 0)
        strcpy(out, cwd);
    else
        sprintf(out, "%s/%s", cwd, in);
    return 0;
}

int main(int argc, char **argv)
{
    static char envbuf[UB_PATH + 16];
    const ub_test *sel[UB_NTESTS];
    double res[UB_NTESTS];
    int kept[UB_NTESTS];
    int nsel = 0, long_passes = UB_LONG_PASSES, quick = 0;
    int i, nindexed = 0;
    double logsum = 0.0;
    const char *tmparg = NULL;
    char dir[UB_PATH], *slash;
    struct utsname u;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0) {
            ub_verbose = 1;
        } else if (strcmp(argv[i], "-q") == 0) {
            quick = 1;
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            long_passes = atoi(argv[++i]);
            if (long_passes < 1 || long_passes > UB_MAX_PASSES) {
                printf("passes must be 1-%d\n", UB_MAX_PASSES);
                return 1;
            }
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            tmparg = argv[++i];
        } else if (argv[i][0] == '-') {
            ub_usage();
            return 1;
        } else {
            const ub_test *t = ub_find(argv[i]);

            if (t == NULL) {
                printf("unknown test \"%s\"\n\n", argv[i]);
                ub_usage();
                return 1;
            }
            if (nsel < UB_NTESTS)
                sel[nsel++] = t;
        }
    }
    if (nsel == 0)
        for (nsel = 0; nsel < UB_NTESTS; ++nsel)
            sel[nsel] = &ub_tests[nsel];

    /* The programs are where ubrun is. */
    if (strlen(argv[0]) >= sizeof(dir)) {
        printf("path too long\n");
        return 1;
    }
    strcpy(dir, argv[0]);
    slash = strrchr(dir, '/');
    if (slash == NULL)
        strcpy(dir, ".");
    else if (slash == dir)
        dir[1] = '\0';
    else
        *slash = '\0';
    if (ub_abspath(ub_bindir, dir) != 0) {
        printf("cannot find the program directory\n");
        return 1;
    }
    if (tmparg != NULL) {
        if (ub_abspath(ub_tmpdir, tmparg) != 0) {
            printf("bad scratch directory\n");
            return 1;
        }
    } else {
        sprintf(ub_tmpdir, "%s/tmp", ub_bindir);
        mkdir(ub_tmpdir, 0777);
    }

    /* execl finds itself, and multi.sh finds tst.sh, through UB_BINDIR;
       multi.sh reads sort.src from the working directory. */
    sprintf(envbuf, "UB_BINDIR=%s", ub_bindir);
    putenv(envbuf);
    putenv("LANG=C");
    if (chdir(ub_bindir) != 0) {
        perror(ub_bindir);
        return 1;
    }

    printf("UnixBench %s\n", UB_VERSION);
    if (uname(&u) >= 0)
        printf("System: %s %s %s; %s\n", u.sysname, u.release, u.version,
               u.machine);
    printf("Programs: %s\nScratch:  %s\n\n", ub_bindir, ub_tmpdir);

    for (i = 0; i < nsel; ++i) {
        int passes = sel[i]->long_run ? long_passes : UB_SHORT_PASSES;
        int seconds = sel[i]->seconds;

        if (quick) {
            passes = 1;
            seconds = seconds > 30 ? 10 : 2;
        }
        kept[i] = ub_run_test(sel[i], passes, seconds, &res[i]);
    }

    printf("\n%-40s %14s %5s\n", "Benchmark Run", "Score", "Passes");
    for (i = 0; i < nsel; ++i) {
        if (kept[i] == 0)
            printf("%-40s %14s\n", sel[i]->desc, "FAILED");
        else
            printf("%-40s %14.1f %-5s (%d)\n", sel[i]->desc, res[i],
                   sel[i]->unit, kept[i]);
    }

    for (i = 0; i < nsel; ++i)
        if (kept[i] != 0)
            ++nindexed;
    if (nindexed > 0) {
        char label[80];

        printf("\n%-40s %10s %10s %8s\n", "System Benchmarks Index Values",
               "BASELINE", "RESULT", "INDEX");
        for (i = 0; i < nsel; ++i) {
            double ratio;

            if (kept[i] == 0)
                continue;
            ratio = res[i] / sel[i]->baseline;
            if (ratio > 0.000001)
                logsum += log(ratio);
            printf("%-40s %10.1f %10.1f %8.1f\n", sel[i]->desc,
                   sel[i]->baseline, res[i], ratio * 10.0);
        }
        printf("%-40s %10s %10s %8s\n", "", "", "", "========");
        if (nindexed == UB_NTESTS)
            strcpy(label, "System Benchmarks Index Score");
        else
            sprintf(label, "Partial Index (%d of %d index tests)",
                    nindexed, UB_NTESTS);
        printf("%-62s %8.1f\n", label, exp(logsum / nindexed) * 10.0);
    }
    return 0;
}
