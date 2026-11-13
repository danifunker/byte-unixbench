#!/bin/sh
# Build the classic Mac OS applications (68k, PowerPC and fat) with Retro68.
#
# Usage: ./build-mac.sh          from UnixBench/portable
#        ./build-mac.sh clean
#
# Needs a Retro68 toolchain (https://github.com/autc04/Retro68) with both the
# 68k and the PowerPC compilers, plus cmake.  RETRO68 names the toolchain
# directory (the one with bin/, m68k-apple-macos/ and powerpc-apple-macos/);
# by default it is found from m68k-apple-macos-gcc on PATH, or taken to be
# ~/repos/Retro68-build/toolchain.  Retro68's host tools (Rez, Elf2Mac, ...)
# are linked against Boost; if that Boost is not on the loader path, set
# RETRO68_BOOST_LIB to its lib directory (~/.local/boost/lib is tried).
#
# Outputs, in build-mac/:
#   UnixBench.bin            fat application: 68020+FPU code and PowerPC code
#   UnixBench-68k.bin        68k only, 68020 with 68881/68882/68040 FPU
#   UnixBench-68k-nofpu.bin  68k only, 68000 with software floating point
#   UnixBench-PPC.bin        PowerPC only
#   UnixBench.dsk            1440K HFS floppy image with all four
#                            applications, for emulators or a real floppy
#   UnixBench.hda            4 MB Apple Partition Map SCSI disk image with
#                            the same files, for QEMU q800, the MiSTer
#                            Quadra core or BlueSCSI (only if rb-cli is on
#                            PATH; https://github.com/danifunker/rusty-backup)
# The .bin files are MacBinary II.  Each build directory under build-mac/
# also keeps Retro68's own outputs (.APPL, .dsk, .ad) for that variant.
set -eu
cd "$(dirname "$0")"

OUT=build-mac
if [ "${1:-}" = clean ]; then
    rm -rf "$OUT"
    exit 0
fi

if [ -z "${RETRO68:-}" ]; then
    gcc=$(command -v m68k-apple-macos-gcc 2>/dev/null || true)
    if [ -n "$gcc" ]; then
        RETRO68=$(cd "$(dirname "$gcc")/.." && pwd)
    elif [ -d "$HOME/repos/Retro68-build/toolchain" ]; then
        RETRO68=$HOME/repos/Retro68-build/toolchain
    else
        echo "Retro68 toolchain not found; set RETRO68" >&2
        exit 1
    fi
fi
PATH=$RETRO68/bin:$PATH
export PATH
if [ -n "${RETRO68_BOOST_LIB:-}" ]; then
    LD_LIBRARY_PATH=$RETRO68_BOOST_LIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
    export LD_LIBRARY_PATH
elif ! Rez --help >/dev/null 2>&1 && [ -d "$HOME/.local/boost/lib" ]; then
    LD_LIBRARY_PATH=$HOME/.local/boost/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
    export LD_LIBRARY_PATH
fi
if ! Rez --help >/dev/null 2>&1; then
    echo "Rez does not run (missing Boost libraries?); set RETRO68_BOOST_LIB" >&2
    exit 1
fi
for t in cmake hformat hmount hcopy humount; do
    command -v $t >/dev/null 2>&1 || { echo "$t not found" >&2; exit 1; }
done

# build <subdir> <toolchain file> <app name> <cpu flags>
build() {
    echo "== $3 ($4)"
    cmake -S . -B "$OUT/$1" -DCMAKE_TOOLCHAIN_FILE="$RETRO68/$2" \
        -DUB_APP_NAME="$3" -DUB_CPUFLAGS="$4" >/dev/null
    cmake --build "$OUT/$1" -- ${MAKEFLAGS:--j4} | grep -v "^make\|^\[" || true
    cp "$OUT/$1/$3.bin" "$OUT/$3.bin"
}

build 68k       m68k-apple-macos/cmake/retro68.toolchain.cmake     UnixBench-68k       "-m68020 -m68881"
build 68k-nofpu m68k-apple-macos/cmake/retro68.toolchain.cmake     UnixBench-68k-nofpu ""
build ppc       powerpc-apple-macos/cmake/retroppc.toolchain.cmake UnixBench-PPC       ""

# Fat application: the 68k CODE resources (--copy) and the PowerPC code
# fragment in the data fork (--data), described by the 'cfrg' resource from
# Retro68's RetroPPCAPPL.r, which also supplies the SIZE resource.  On a 68k
# Mac the Process Manager ignores cfrg and runs CODE; on a PowerPC Mac it
# prefers the fragment.
echo "== UnixBench (fat)"
RINC=$RETRO68/powerpc-apple-macos/RIncludes
Rez "$RINC/RetroPPCAPPL.r" -I "$RINC" -DCFRAG_NAME='"UnixBench"' \
    --copy "$OUT/68k/UnixBench-68k.code.bin" \
    --data "$OUT/ppc/UnixBench-PPC.pef" \
    -t APPL -c UBch -o "$OUT/UnixBench.bin"
ResInfo -a "$OUT/UnixBench.bin"

# A 1440K HFS floppy image with every application; together they are under
# 500 KB.  hcopy -m decodes MacBinary into both forks and the Finder info;
# the file names come from the MacBinary headers.
echo "== UnixBench.dsk"
DSK=$OUT/UnixBench.dsk
rm -f "$DSK"
dd if=/dev/zero of="$DSK" bs=1024 count=1440 2>/dev/null
hformat -l UnixBench "$DSK" >/dev/null
hcopy -m "$OUT/UnixBench.bin" "$OUT/UnixBench-68k.bin" \
    "$OUT/UnixBench-68k-nofpu.bin" "$OUT/UnixBench-PPC.bin" :
hls -l
humount

# APM SCSI disk with an Apple driver partition, which a Quadra ROM (real,
# QEMU's q800 or the MiSTer core) mounts as a second hard disk.  hfsutils
# can't make one, so this needs rb-cli.
if command -v rb-cli >/dev/null 2>&1; then
    echo "== UnixBench.hda"
    HFS=$OUT/UnixBench.hfs
    rm -f "$HFS" "$OUT/UnixBench.hda"
    rb-cli --progress never -q new --fs hfs --size 4M --name UnixBench "$HFS"
    for f in UnixBench.bin UnixBench-68k.bin UnixBench-68k-nofpu.bin UnixBench-PPC.bin; do
        rb-cli --progress never -q put-macbinary "$HFS" "$OUT/$f"
    done
    rb-cli --progress never -q expand --size 4M --output "$OUT/UnixBench.hda" "$HFS"
    rb-cli --progress never -q mac-scsi-bless "$OUT/UnixBench.hda"
    rm -f "$HFS"
    rb-cli --progress never -q ls "$OUT/UnixBench.hda@1" /
else
    echo "== UnixBench.hda skipped (rb-cli not on PATH)"
fi

echo
ls -l "$OUT"/*.bin "$OUT"/*.dsk "$OUT"/*.hda 2>/dev/null
