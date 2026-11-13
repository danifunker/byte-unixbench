# Resume prompt: classic Mac OS port of the UnixBench portable harness

Paste everything below the line into Claude Code on the Linux box, from the
repo root (`byte-unixbench/`).

---

Port the UnixBench portable harness in `UnixBench/portable/` to classic
Mac OS as a **fat application (68k + PowerPC)**, using the Retro68 toolchain
already installed on this machine. Only the CPU tests are in scope (Dhrystone,
Whetstone, arith, hanoi), the same set as the NT/DOS builds.

## Context

- `UnixBench/portable/` is a single-executable harness. All OS-specific code
  is behind the six functions in `plat.h`: `plat_init`, `plat_seconds`,
  `plat_alarm_start`, `plat_alarm_end`, `plat_sysinfo`, `plat_check_target`.
  The existing layers `plat_nt.c`, `plat_dos.c` and `plat_psx.c` are the
  models to follow. The tests (`t_*.c`) and the runner (`ubench.c`) should not
  need changes. If one does, keep it minimal and explain why.
- `README.md` has a "Classic Mac OS: planned design" section, and the header
  comment in `plat.h` lists the constraints the interface was built around.
  Read both first.
- Code rules: strict C89 (no `//` comments, no `long long`/`__int64`, no
  declarations after statements), LF line endings. `Makefile.posix` builds
  with `-std=c89 -pedantic` to check this.
- Tests read sources from `../src` (e.g. `src/dhry_2.c`, `src/whets.c`).

## What to build

1. **`plat_mac.c`**
   - `plat_init`: Toolbox setup if the console library doesn't already do it.
     There is no command line. Keep the defaults (all tests), or, if Retro68's
     console makes it easy, read an optional argument line the way
     CodeWarrior's `ccommand()` does.
   - `plat_seconds`: `Microseconds()` returns an `UnsignedWide`. Convert it as
     `hi * 4294967296.0 + lo` (no 64-bit ints).
   - `plat_alarm_start/end`: a Time Manager task (`InsTime` + `PrimeTime(task,
     seconds * 1000)`, `RmvTime` in end). The handler may only store to the
     flag: it runs at interrupt time.
     - **68k**: at interrupt time A5 may not point to the app's globals. Use an
       extended struct `{ TMTask tm; volatile int *flag; }` and recover it
       from A1 inside the handler (Retro68 needs inline asm or a
       register-parameter function; see how Retro68 samples or headers declare
       `TimerProcPtr` for 68k). Also check whether Retro68's 68k runtime even
       uses A5 for globals. If it doesn't, document that and keep the struct
       trick anyway for safety.
     - **PPC**: wrap the handler with `NewTimerUPP`. The A5 issue doesn't
       arise.
     - If `InsTime` fails or the flag never fires, set the flag at once, like
       `plat_nt.c` does, rather than spin forever.
   - `plat_sysinfo`: Gestalt for the system version (`gestaltSystemVersion`),
     the CPU (`gestaltNativeCPUtype`, decoded for 68000-68040, 601/603/603e/
     604/604e/750 at least), the FPU on 68k (`gestaltFPUType`), RAM
     (`gestaltPhysicalRAMSize`) and the machine name (`gestaltMachineType` +
     `GetIndString(..., kMachineNameStrID, ...)` if available). Also say
     whether the 68k code is running under the PPC emulator
     (`gestaltSysArchitecture`).
   - `plat_check_target`: report "68k generic", "68k 68020+FPU" etc. or "PowerPC".
     If a 68k build compiled with `-m68881` runs on a machine with no FPU,
     return 0. Running the 68k code under the PPC emulator is allowed, but
     the Build/System line should warn that the scores are emulated.
2. **`build-mac.sh`** (and a Makefile or CMakeLists, whichever fits Retro68
   best; see `Retro68/Samples/HelloWorld` and its console/SIOUX-like
   `RetroConsole` support). Outputs:
   - a 68k app (generic 68020 + 68881, or a no-FPU variant as well if it's cheap),
   - a PPC app,
   - a **fat app**: 68k `CODE` resources and PPC PEF data fork + `cfrg` in one
     file. Check whether Retro68's CMake supports this directly. If not,
     merge the two with Retro68's `Rez` (`--copy` the 68k resources, `--data`
     the PPC PEF, include `cfrg`). Verify the result with `ResInfo`/`DeRez`.
   - Output formats: MacBinary `.bin` plus a `.dsk` disk image containing the
     fat app, ready to copy into an emulator or onto real hardware.
   - Use `-O2`/`-O3 -ffast-math` for the kernels and `-O0` for `t_arith.c`,
     matching the other builds (see `Makefile.dos`/`Makefile.nt` for the
     reason).
3. **Output**: show results in the console window. Also write the same text
   to a file (e.g. `UnixBench Results` next to the app) so it can be copied
   off the machine. The window should stay open at the end until a key or
   click.
4. **Test** in an emulator if one is installed here: Basilisk II or Mini vMac
   for 68k, SheepShaver or QEMU `-M mac99` for PPC. Run with `-q`-like
   settings first. Check that the alarm stops each test at about the
   requested time and that scores are plausible.
5. **Docs**: replace the "planned design" section in `README.md` with a
   "Building for classic Mac OS" section (toolchain, `build-mac.sh`, outputs,
   timing/alarm notes, emulator caveat). Update the `plat.h` header comment
   to list `plat_mac.c`. Add build outputs to `.gitignore`.

Don't commit unless I ask. Report what was built, sizes, what was tested in
which emulator, and anything unverified.
