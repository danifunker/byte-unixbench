# UnixBench portable harness

A single-executable build of UnixBench's CPU tests for systems that can't
run the regular Perl/`make`/`fork` harness: **Windows NT** (the first
target was NT 4.0 on PowerPC), **MS-DOS** and **classic Mac OS** (68k and
PowerPC). There is also a static build for **NetBSD/sparc**, for machines
where one binary is handier than the full suite.

| Test | Source | Indexed | Baseline |
|---|---|---|---|
| `dhry2reg` | `src/dhry_1.c` (adapted) + `src/dhry_2.c` (unchanged) | yes | 116700 lps |
| `whetstone-double` | `src/whets.c` (unchanged, `#include`d) | yes | 55.0 MWIPS |
| `arithoh`, `short`, `int`, `long`, `float`, `double` | `src/arith.c` (adapted) | no | — |
| `hanoi` | `src/hanoi.c` (adapted) | no | — |

"Adapted" means the benchmark body is unchanged, but the timing that
surrounds it is different (see *How it differs from the Unix build*).

## Building on Windows NT

Copy the whole `UnixBench` directory to the NT machine; the build reads
`..\src`. Then, from a command prompt with the compiler environment set
(`VCVARS32.BAT` for Visual C++ 4.x, or a Developer Command Prompt for
current Visual Studio):

```
cd UnixBench\portable
nmake /f Makefile.nt
ubench
```

The binary targets whatever CPU `cl.exe` targets. On NT 4.0 for PowerPC,
use the PowerPC edition of Visual C++ 4.x. A current Visual Studio
produces x86/x64/ARM64 binaries, which are useful for checking the Win32
code path but are not PowerPC builds.

The code is C89 throughout. It uses no `__int64`, `long long`, `//`
comments or declarations after statements. The only Win32 calls are ones
NT 3.51 already had: `QueryPerformanceCounter`, `CreateThread`, `Sleep`,
`GetVersionEx` and `GetSystemInfo`. The source files use LF line endings.
`cl` and `nmake` accept them, but Notepad shows them as one long line.

## Building for PINT (NT 4.0 on ARM)

[PINT](https://github.com/tenox7/PINT) is Windows NT 4.0 on the Raspberry
Pi. Its SDK cross-compiles from Docker, so no NT machine is needed:

```
./build-pint.sh
```

That makes `ubench-arm-pint.exe`. The header of `build-pint.sh` has the
one-time step that installs Microsoft C for ARM, which accepts the Visual
Studio Build Tools licence.

## Building for MS-DOS (x86)

`build-dos.sh` cross-compiles with [DJGPP](https://www.delorie.com/djgpp/)
(GCC 12.2 from [build-djgpp](https://github.com/andrewwutw/build-djgpp))
in Docker. The first run downloads the toolchain and CWSDPMI into the
`djgpp-cross` volume.

```
./build-dos.sh              # ubench.exe: generic, any 386 or later
./build-dos.sh variants     # also ub486.exe, ub586.exe, ub686.exe
```

The binaries are 32-bit protected-mode programs. CWSDPMI is bound into
each `.exe`, so it runs from plain DOS with nothing alongside it. Under
Windows 3.x/9x or in an NT DOS box, the host's DPMI is used instead.
DJGPP users on DOS can run `make -f Makefile.dos` directly.

- **Timing:** `uclock()` (8254 PIT, 0.84 us). At start-up it is checked
  against the BIOS tick, and if they disagree the BIOS tick is used (55 ms,
  about 0.5% of a 10 s pass). The `System:` line says which clock was used.
- **Alarm:** a protected-mode handler chained onto IRQ 0. It is removed
  at exit and on Ctrl-C. (DJGPP's `setitimer()` never fired under DOSBox-X.)
- **FPU:** Whetstone needs an x87. Without one, DJGPP needs `emu387.dxe`,
  and the scores are meaningless anyway.
- **Optimization:** `-O3 -ffast-math` like the UnixBench Makefile, with
  arith at `-O0`, because GCC deletes its kernel otherwise.
- **Variants:** `ub486`/`ub586`/`ub686` work like the Alpha builds. They
  check the CPU (family via CPUID or the EFLAGS AC/ID bits, plus CMOV for
  686) and refuse to run on an older one unless you pass `-f`.

## Building for SPARC (NetBSD)

`build-sparc.sh` cross-compiles for 32-bit SPARC in Docker with Debian
bookworm's `sparc64-linux-gnu-gcc` (GCC 12.2, binutils 2.40) and `-m32`.
The first run downloads the NetBSD 11.0 `sparc` sets `base.tgz` and
`comp.tgz` (190 MB) and keeps their headers and libraries in the
`sparc-netbsd-cross` volume.

```
./build-sparc.sh            # all three binaries
./build-sparc.sh netbsd     # only the NetBSD ones
./build-sparc.sh linux      # only the Linux one
```

| Binary | Runs on | Kernels compiled with |
|---|---|---|
| `ubench-sparc-netbsd` | NetBSD/sparc on any 32-bit SPARC | `-mcpu=v7` |
| `ubench-sparc-netbsd-v8` | NetBSD/sparc on SPARC V8: SuperSPARC, microSPARC, hyperSPARC and later | `-mcpu=supersparc` |
| `ubench-sparc-linux` | Linux with a 32-bit SPARC userland, on a V9 CPU; `qemu-sparc32plus` | `-mcpu=v7` |

Ready-built copies of the two NetBSD binaries are in `release/`. They are
static, so they need nothing but the kernel: copy one over and run it.

- **Toolchain:** the compiler targets Linux. For NetBSD it is used with
  `-nostdinc` and NetBSD's headers, the Linux predefines swapped for
  `__NetBSD__`, and a hand-written link line: NetBSD's `crt0.o`, `crti.o`,
  `crtbeginT.o`, `libm.a`, `libc.a`, `libgcc.a`, `crtend.o` and `crtn.o`,
  nothing from Debian. The NetBSD ident note comes from `crti.o`. Both
  binaries were run on NetBSD 11.0 on a SPARCstation 20 (the MiSTer core,
  SuperSPARC at 55 MHz).
- **Generic build:** V7 has no multiply or divide instruction, so the
  compiler calls `.umul`, `.div` and friends in NetBSD's libc, which is
  V7 code itself: the whole static binary has no `umul`, `smul`, `udiv` or
  `sdiv` in it.
- **V8 build:** works like the DOS variants. Only the kernels are V8 code
  (`smul` and `sdiv` in Dhrystone, Whetstone and the integer arith tests);
  the runner, the platform layer and libc stay V7. The binary asks the
  kernel for `machdep.cpu_arch` and refuses to run on a V7 CPU unless you
  pass `-f`.
- **Timing and alarm:** `gettimeofday()` and `alarm()`, as on every POSIX
  host (`plat_psx.c`). The `System:` line shows `uname`, and on NetBSD
  `hw.model` and the SPARC version too.
- **Optimization:** `-O3 -ffast-math`, arith at `-O0`, as for MS-DOS.
- **Linux binary:** only there to test the build under QEMU. Debian's
  32-bit SPARC libc is V9 code, so the static binary is a V8+ executable
  (`EM_SPARC32PLUS`): it needs `qemu-sparc32plus`, not `qemu-sparc`, and
  does not run on a real 32-bit SPARC. Its kernels are the same V7 code
  as in the generic NetBSD binary. `make -f Makefile.sparc
  ubench-sparc-linux-v8` builds one with the V8 kernels. Under
  `qemu-sparc32plus` 8.2 some Dhrystone passes end in "wrong answer": when
  the alarm signal lands inside glibc's V9 `strcpy` or `strcmp`, the
  interrupted call returns garbage (a loop of those two calls under a 1 ms
  timer went wrong on about half the signals; a loop of multiplies and
  divides never did). The NetBSD binaries contain no V9 code.

## CPU-specific builds

`ubench.exe` is the **generic** build. It uses no CPU-specific options, so
it runs on every CPU of its architecture, and its results are the ones to
compare across machines. On Alpha, the makefile can also build one binary
per CPU generation:

| Target | Binary | Runs on | What it adds |
|---|---|---|---|
| `ev4` | `ubench-ev4.exe` | 21064 / 21066 / 21068 and later | tuning for EV4 |
| `ev5` | `ubench-ev5.exe` | 21164 and later | tuning for EV5 |
| `ev56` | `ubench-ev56.exe` | 21164A / 21164PC and later | BWX byte/word instructions |
| `ev6` | `ubench-ev6.exe` | 21264 and later | BWX, FIX, MVI |

```
nmake /f Makefile.nt ev56
nmake /f Makefile.nt alpha
```

The first command builds one variant; `alpha` builds all four. Each
variant has its own object directory (`obj-ev56\` etc.), so variant
builds never mix object files.

**The code-generation flags** (`EV4FLAGS`, `EV5FLAGS`, `EV56FLAGS`,
`EV6FLAGS` at the top of `Makefile.nt`) are `/QA21064`, `/QA21164`,
`/QA21164A` and `/QA21264`. Those options are listed in the option table
of the Alpha `cl.exe` 12.01.8433, the compiler in the Windows 2000 build
2113 tools. Each one sets `_M_ALPHA` to the chip and selects that chip's
architecture. Without an option, the compiler builds for EV4 and tunes for
a blend of EV4, EV5 and EV6, so the generic build runs on a 21064.

Older Alpha compilers may lack these options. In that case, `cl` warns
`D4002: ignoring unknown option` and builds a generic binary under the
variant's name, so watch the build output for that warning. A variant
with empty flags refuses to build.

Only the benchmark kernels get the CPU flags. The runner and platform
layer stay generic, so every binary can start on any Alpha and check the
CPU first:

- A binary started on a CPU older than its target stops with a message,
  instead of crashing on an illegal instruction or having the OS emulate
  the instructions, which would make the timings meaningless. `-f`
  overrides the check.
- The `Build:` line under `System:` shows which variant produced the
  results. Record it, and the flags, with any results.

## Usage

```
ubench [-v] [-q] [-f] [-i passes] [test ...]
```

- With no test names, every test runs.
- `-q` is a smoke test: 1 second and 1 pass per test.
- `-i` sets the number of passes for `dhry2reg` and `whetstone-double`
  (default 10, like `Run -i`). The other tests run 3 passes.
- `-v` prints every pass, plus Whetstone's per-section table.
- `-f` runs a CPU-specific build even on a CPU older than its target.

Scoring follows `Run`'s `combinePassResults()`: drop the lowest third of
passes and take the geometric mean of the rest. The index for each test is
`score / baseline × 10`.

**Only 2 of the 12 System Benchmarks index tests are ported**, so the
summary line reads "Partial Index (2 of 12 index tests)". It is not
comparable to a full UnixBench index score.

## Layout

```
plat.h          platform interface (clock, alarm, sysinfo, argv)
plat_nt.c       Win32 implementation
plat_dos.c      MS-DOS (DJGPP) implementation
plat_mac.c      classic Mac OS (Retro68) implementation
mac_console.c   classic Mac OS console window (stdout/stdin)
plat_psx.c      POSIX implementation: NetBSD/sparc, and cross-checking scores
ubench.c/.h     runner: test table, passes, scoring, index, report
t_dhry.c        Dhrystone main loop + Proc_1..Proc_5 (from dhry_1.c)
t_whets.c       wraps src/whets.c unchanged
t_arith.c/.h    six arith variants from one template
t_hanoi.c       Tower of Hanoi
Makefile.nt     NMAKE, for Visual C++ 4.x and later
Makefile.dos    GNU make, for DJGPP (native or cross)
Makefile.posix  for comparing against the stock binaries
Makefile.sparc  GNU make, 32-bit SPARC cross build (NetBSD, Linux)
CMakeLists.txt  classic Mac OS, for Retro68's CMake toolchain files
build-mac.sh    runs CMake for each Mac CPU and makes the fat application
build-dos.sh    DJGPP cross build in Docker
build-sparc.sh  SPARC cross build in Docker
build-pint.sh   NT 4.0 on ARM (PINT) cross build in Docker
```

## How it differs from the Unix build

The Unix build runs each test as its own process. `alarm()` sends
SIGALRM, and the handler prints the count and calls `exit()`. `Run`
measures wall-clock time from outside the process.

The portable build has every test in one executable:

- Each time-limited loop polls a `volatile int ub_stop` instead of being
  interrupted. The platform alarm sets the flag, and the runner times the
  loop itself with the platform's high-resolution clock.
- The extra cost per iteration is one load of a volatile, which is
  negligible next to a Dhrystone pass or 100 arith divides.
- Process start-up time is no longer part of the measurement.
- Globals are re-initialised on each pass, because the tests no longer get
  a fresh process every time.
- On Windows (and classic Mac), `long` is 32 bits. For that reason, arith
  keeps a second counter for wrap-arounds, and counts reach the runner as
  `double`.

Checked on an arm64 Mac against the stock binaries compiled with the same
`-O3 -ffast-math`, 3 passes each:

| Test | Stock | Portable |
|---|---|---|
| Dhrystone | 106.6–109.9M lps | 105.8–107.0M lps |
| Whetstone | 12273–12690 MWIPS | 12518–12558 MWIPS |
| Hanoi | 747.8–750.1k lps | 743.0–751.9k lps |

The arith tests don't compare. A modern optimizer deletes `dumb_stuff()`
in both builds, because its result is never used. What remains is a loop
that only increments a counter: about 2.9G lps in the stock build and
about 1.0G here, where the per-iteration flag and wrap checks dominate
such an empty loop. Older compilers such as Visual C++ 4.x are unlikely
to delete the kernel, but check the arith numbers before you rely on them.

## Building for classic Mac OS

`build-mac.sh` cross-compiles with [Retro68](https://github.com/autc04/Retro68)
(GCC 12 for 68k and PowerPC, with the Multiversal Interfaces) and CMake,
and needs a toolchain built with both the 68k and the PowerPC compilers:

```
./build-mac.sh
```

The script finds the toolchain from `m68k-apple-macos-gcc` on `PATH` or
from `RETRO68` (the directory with `bin/`, `m68k-apple-macos/` and
`powerpc-apple-macos/`). Retro68's host tools are linked against Boost;
if they fail to start, set `RETRO68_BOOST_LIB` to the directory with those
libraries. `build-mac.sh clean` removes `build-mac/`.

Outputs in `build-mac/`:

| File | Contents | Runs on |
|---|---|---|
| `UnixBench.bin` | **fat application**: 68020+FPU `CODE` resources plus a PowerPC code fragment | 68020/68030/68040 Macs with an FPU; every PowerPC Mac |
| `UnixBench-68k.bin` | 68020 with 68881/68882 (or 68040 FPU) code, `-m68020 -m68881` | same 68k Macs |
| `UnixBench-68k-nofpu.bin` | 68000 code, software floating point | every 68k Mac from the Plus up, including 68LC040 models, and the 68k emulator of PowerPC Macs |
| `UnixBench-PPC.bin` | PowerPC only | every PowerPC Mac |
| `UnixBench.dsk` | 1440K HFS floppy image with all four applications | emulators (Basilisk II, SheepShaver, Mini vMac), or written to a floppy for a real Mac |
| `UnixBench.hda` | 4 MB Apple Partition Map SCSI disk with the same files (needs [rb-cli](https://github.com/danifunker/rusty-backup)) | QEMU `q800` as a second SCSI disk, the MiSTer Quadra 800 core's disk slots, BlueSCSI |

Ready-built copies of all of these are in `release/`.

The `.bin` files are MacBinary II. StuffIt Expander or BinHex opens them
on the Mac; on the host, `hcopy -m` (hfsutils) writes them into an HFS
image with both forks and the Finder info, which is how the `.dsk` image
is made. On a 68k Mac the Process Manager runs the fat application's
`CODE` resources; a PowerPC Mac prefers the code fragment named by the
`cfrg` resource. The applications are small: 75 KB (68k), 102 KB (no
FPU, because of the software floating point), 117 KB (PowerPC) and
192 KB (fat).

**Running.** Double-click the application. There is no command line, so
the console window first asks for options, which are the same as on the
other platforms (`-q`, `-v`, `-i passes`, `-f`, test names); Return alone
runs every test with the defaults. Everything printed is also written to
`UnixBench Results` (a SimpleText file with Mac line endings) next to the
application, or on the desktop of the startup disk if the application's
volume is locked, e.g. a CD-ROM. At the end the window stays open until
Return is pressed or its close box is clicked (Command-Q quits too). The
Mac does nothing else while a pass runs: the window is not redrawn, the
clock stops and the mouse is ignored until the pass ends.

**Console.** `mac_console.c` is a small console of its own (a window of
Monaco 9 cells, no scrollback, a line editor for input) hooked into
Retro68's C library through `_consolewrite()` and `_consoleread()`.
Retro68's RetroConsole library was tried first; it is C++, which made the
application 550 KB on 68k and 1 MB on PowerPC, and it exposed a Retro68
bug: with libstdc++ linked in, `ld -q` emits two `R_68K_32` relocations
for one `.eh_frame` word, Elf2Mac (November 2025) encodes the zero
distance between them as the 0 byte that ends its relocation list, and
the start-up code then patches the wrong words (the FPU build died with an
F-line error inside libstdc++ at launch; the no-FPU build survived by
luck). Newer Elf2Mac stops with "Duplicate Relocation present" instead.
Without C++ the relocation lists are clean.

**Clock and alarm.**

- The clock is `Microseconds()`, with `TickCount()` (60.15 Hz) on systems
  without that trap (System 6). The 64-bit microsecond count is combined
  in `double` arithmetic, so no 64-bit integer type is needed. The
  `System:` line says which clock was used.
- The alarm is a Time Manager task: `InsTime`, `PrimeTime(ms)`, `RmvTime`.
  The task routine runs at interrupt time and only stores to the flag.
  On 68k the Time Manager passes the task record in A1 and A5 may not
  point to the application's globals, so the record is extended with the
  flag pointer and the routine, declared with Retro68's
  `#pragma parameter`, takes it from A1 and touches no global. (Retro68
  does not address globals through A5 anyway: Elf2Mac emits absolute
  addresses that the start-up code relocates. The routine is written to
  be safe under either model.) On PowerPC the routine is reached through
  a Mixed Mode routine descriptor. The Multiversal `TMTask` lacks the
  revised Time Manager's two trailing fields, so `plat_mac.c` declares
  the full 22-byte record.
- At start-up a 50 ms test alarm is fired. If it never arrives, the
  `System:` line says `ALARM FAILED` and each test stops at once instead
  of spinning forever, like `plat_nt.c` does when it cannot start its
  timer thread.
- `plat_sysinfo` uses Gestalt (system version, CPU, FPU, RAM, machine
  name, whether 68k code is being emulated) and falls back to
  `SysEnvirons` on a System 6 without Gestalt.

**CPU variants.** `-m68881` changes how the compiler returns `double`
values, so the whole 68k binary, C library included, has to agree:
`UB_CPUFLAGS` in `CMakeLists.txt` applies to every file, not just the
kernels, and the link picks the matching Retro68 multilib. Retro68's
console library exists only as a software-float build, which is fine
because no floating-point value crosses into it. `plat_check_target`
reads the compiler's macros: the FPU build reports `68k 68020+FPU` and
refuses to run on a machine without an FPU, which includes the 68k
emulator of a PowerPC Mac (it emulates a 68LC040); `-f` overrides the
check. The FPU build uses the 68881 instruction set, so on a 68040 the
transcendental instructions and `fmovecr` that GCC emits for `-ffast-math`
are handled by the ROM's FPSP emulation; `UB_CPUFLAGS=-m68040` avoids
most of them if that matters. The no-FPU build reports `68k generic` and runs anywhere, but when
it runs under the PowerPC 68k emulator the `Build:` line warns that the
scores are not native. The PowerPC build is generic (601-compatible) code.

**Emulators.** The `.dsk` file is a bare HFS volume for Basilisk II,
SheepShaver and Mini vMac. QEMU's `q800` machine and the MiSTer Quadra 800
core want a SCSI disk instead, which is what `UnixBench.hda` is (an Apple
Partition Map with an Apple driver partition); `qemu-system-ppc -M mac99`
with Mac OS 9 can mount an HFS image as a CD-ROM (`-cdrom`). Scores from an
emulator describe the host, not a Mac, and not even consistently: under
QEMU's `q800` the 68020+FPU build scored several times lower than the
no-FPU build on Dhrystone and Hanoi with equivalent code and no extra
exceptions logged, presumably a translation artifact. What to check in an
emulator is that the `System:` and `Build:` lines are right, that `-v`
shows each pass ending at the requested time, and that `UnixBench Results`
appears.

## Not ported

- Process, pipe, file system and shell tests, which depend on
  `fork`/`exec`/`pipe`/`/bin/sh`.
- Parallel copies (the "N copies" index). On NT this would mean launching
  `ubench.exe` again for each CPU with `CreateProcess`. Classic Mac has no
  equivalent.
