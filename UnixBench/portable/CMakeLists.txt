# UnixBench portable harness - classic Mac OS build (Retro68 + CMake)
#
# Only the classic Mac OS targets use CMake; the other platforms have
# Makefile.nt, Makefile.dos and Makefile.posix.  build-mac.sh configures this
# project once per CPU and merges the 68k and PowerPC results into a fat
# application, so normally run that instead:
#
#   cmake -S . -B build-mac/68k \
#       -DCMAKE_TOOLCHAIN_FILE=$RETRO68/m68k-apple-macos/cmake/retro68.toolchain.cmake \
#       -DUB_APP_NAME=UnixBench-68k -DUB_CPUFLAGS="-m68020 -m68881"
#   cmake --build build-mac/68k
#
# Variables:
#   UB_APP_NAME    application name; also the Mac file name
#   UB_CPUFLAGS    CPU/FPU options.  They apply to every file, not just the
#                  kernels, because -m68881 changes how doubles are returned
#                  and the whole binary (libc and libm included) must agree.
#                  "-m68020 -m68881" = 68020 with FPU; empty = 68000 with
#                  software floating point.  plat_mac.c reads the resulting
#                  compiler macros for the Build: line and the CPU check.
#   UB_OPTON       kernel optimization, -O3 -ffast-math like the UnixBench
#                  Makefile; quote it along with any results
#   UB_ARITHFLAGS  t_arith.c optimization, -O0: GCC deletes dumb_stuff()
#                  otherwise and measures an empty loop (see Makefile.dos)

cmake_minimum_required(VERSION 3.13)
project(ubench-mac C)

set(UB_APP_NAME   "UnixBench"       CACHE STRING "application name")
set(UB_CPUFLAGS   ""                CACHE STRING "CPU/FPU options for every file")
set(UB_OPTON      "-O3 -ffast-math" CACHE STRING "kernel optimization")
set(UB_ARITHFLAGS "-O0"             CACHE STRING "t_arith.c optimization")

if(NOT CMAKE_SYSTEM_NAME MATCHES "^Retro")
    message(FATAL_ERROR "Pass a Retro68 toolchain file: "
        "-DCMAKE_TOOLCHAIN_FILE=.../retro68.toolchain.cmake or .../retroppc.toolchain.cmake")
endif()

separate_arguments(UB_CPUFLAGS_LIST   UNIX_COMMAND "${UB_CPUFLAGS}")
separate_arguments(UB_OPTON_LIST      UNIX_COMMAND "${UB_OPTON}")
separate_arguments(UB_ARITHFLAGS_LIST UNIX_COMMAND "${UB_ARITHFLAGS}")

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# mac_console.c provides the console window, so Retro68's RetroConsole
# (the CONSOLE option, C++) is not used; see its header comment for why.
add_application(${UB_APP_NAME}
    ubench.c plat_mac.c mac_console.c
    t_dhry.c ${SRC}/dhry_2.c t_whets.c t_arith.c t_hanoi.c
    TYPE APPL
    CREATOR UBch)

# The code is C89, but the Multiversal headers need gnu89 (// comments).
# Makefile.posix is the strict -std=c89 -pedantic check for the shared files.
target_compile_options(${UB_APP_NAME} PRIVATE
    -std=gnu89 -Wall -ffunction-sections ${UB_CPUFLAGS_LIST} ${UB_OPTON_LIST})

# dhry2reg flags, as in the UnixBench Makefile.  TIME keeps dhry.h from
# including <sys/times.h>.
set_source_files_properties(t_dhry.c ${SRC}/dhry_2.c PROPERTIES
    COMPILE_DEFINITIONS "TIME;REG=register")
# Source-level options come after the target's, so -O0 overrides UB_OPTON.
set_source_files_properties(t_arith.c PROPERTIES
    COMPILE_OPTIONS "${UB_ARITHFLAGS_LIST}")

# The CPU flags select the matching multilib (libc, libm, libgcc) at link
# time.  gc-sections and the MacsBug-name strip keep the 68k binary small;
# --strip-debug keeps the .gdb file Elf2Mac leaves behind small.
target_link_libraries(${UB_APP_NAME} m)
target_link_options(${UB_APP_NAME} PRIVATE ${UB_CPUFLAGS_LIST} -Wl,-gc-sections)
if(CMAKE_SYSTEM_NAME MATCHES Retro68)
    target_link_options(${UB_APP_NAME} PRIVATE -Wl,--mac-strip-macsbug -Wl,--strip-debug)
endif()
